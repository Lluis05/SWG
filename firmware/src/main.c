/*
 * SowGuard node firmware — nRF52840-DK
 *
 * Based on Nordic's peripheral_lbs sample (LicenseRef-Nordic-5-Clause).
 *
 * The board is a BLE peripheral. It advertises our service, and once a phone
 * connects and enables notifications, it sends one 15-byte frame every 2 s.
 * Frame layout: docs/protocol.md (version 1).
 *
 * Temperature and humidity are REAL (DHT11 on P0.03, see the overlay in
 * boards/). Water flow is REAL too: pulses of an SW-420 vibration sensor on
 * P0.04 (Lab proxy; the Design uses a pulse flow meter). Posture is REAL:
 * tilt measured by an MPU-9250 accelerometer on I2C (SDA P1.04, SCL P1.06).
 * Piglet scream and crush alarm are SIMULATED with the DK buttons (allowed by
 * the professor).
 *
 * Buttons (lab simulation of the design's sensors):
 *   Button 1  not used (was the simulated posture)
 *   Button 2  piglet scream               (design: microphone)
 *   Button 3  not used (was the simulated water drop)
 *   Button 4  simulate a DHT11 failure on / off (to test the sensor-error flag)
 * LEDs:
 *   LED1 phone connected · LED2 crush alarm · LED3 sow lying · LED4 blinks (running)
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <dk_buttons_and_leds.h>

#define DEVICE_NAME      CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN  (sizeof(DEVICE_NAME) - 1)

#define CON_STATUS_LED   DK_LED1   /* on: a phone is connected */
#define ALARM_LED        DK_LED2   /* on: crush alarm active */
#define LYING_LED        DK_LED3   /* on: the sow is lying down */
#define RUN_STATUS_LED   DK_LED4   /* blinks: the program is running */

#define SCREAM_BUTTON    DK_BTN2_MSK
#define SENSOR_FAIL_BUTTON DK_BTN4_MSK

#define SEND_PERIOD_MS   2000      /* one frame every 2 s (protocol.md) */
#define FRAME_LEN        15        /* bytes per frame (protocol.md) */
#define FRAME_VERSION    1

/* Simulation parameters (agreed 4 Nov). Demo values, NOT real measurements. */
#define SCREAM_WINDOW_MS   30000   /* a scream counts only up to 30 s after lying down */

/* Water flow from the vibration sensor (section 6).
 * Calibrated 23 Nov with the UART log (pulses per 2 s window):
 * still 0, tapping 17-80, shaking 200-405. With 5: tapping ~85-400 mL/min,
 * shaking reaches the 600 limit. */
#define WATER_ML_PER_PULSE 5       /* mL/min added by each pulse in a 2 s window */
#define WATER_WINDOWS      3       /* average of the last 3 windows (6 s) */
#define WATER_MAX          600     /* upper limit, mL/min */
#define WATER_LOW_LIMIT    200     /* below this -> low-water flag (agreed 4 Nov) */


/* ------------------------------------------------------------------------
 * 1. UUIDs and GATT service
 * ------------------------------------------------------------------------ */

/* UUIDs from docs/protocol.md:
 *   service 5781a4a8-0001-41a3-b91f-baa566bff75f
 *   data    5781a4a8-0002-41a3-b91f-baa566bff75f
 * BT_UUID_128_ENCODE takes the 5 groups of the UUID written as numbers. */
#define SOWGUARD_SERVICE_UUID_VAL \
	BT_UUID_128_ENCODE(0x5781a4a8, 0x0001, 0x41a3, 0xb91f, 0xbaa566bff75f)
#define SOWGUARD_DATA_UUID_VAL \
	BT_UUID_128_ENCODE(0x5781a4a8, 0x0002, 0x41a3, 0xb91f, 0xbaa566bff75f)

static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(SOWGUARD_SERVICE_UUID_VAL);
static struct bt_uuid_128 data_uuid = BT_UUID_INIT_128(SOWGUARD_DATA_UUID_VAL);

/* true when the phone has enabled notifications (wrote 0x0001 to the CCCD). */
static bool notify_enabled;

/* Called by the BLE stack when the phone writes the CCCD. */
static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Notifications %s\n", notify_enabled ? "enabled" : "disabled");
}

/* Our GATT service. The stack builds this table of "attributes":
 *   attrs[0] service declaration       (our service UUID)
 *   attrs[1] characteristic declaration (properties: Notify)
 *   attrs[2] characteristic value       (the 15-byte frame)
 *   attrs[3] CCCD                       (the phone writes 0x0001 here)
 * No read/write callbacks: the value is only sent by notification.
 * BT_GATT_PERM_NONE + no pairing = no security, as decided on 28 Oct. */
BT_GATT_SERVICE_DEFINE(sowguard_svc,
	BT_GATT_PRIMARY_SERVICE(&service_uuid),
	BT_GATT_CHARACTERISTIC(&data_uuid.uuid, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);


/* ------------------------------------------------------------------------
 * 2. Advertising
 * ------------------------------------------------------------------------ */

/* Advertising packet (max 31 bytes): flags (3 B) + our service UUID (18 B).
 * The app scans with a filter on this UUID, so it MUST be here. */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SOWGUARD_SERVICE_UUID_VAL),
};

/* Scan response (sent when the phone asks for more): the device name. */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static struct k_work adv_work;

/* Runs in the system work queue (not inside a BLE callback). */
static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad),
				  sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}
	printk("Advertising successfully started\n");
}

static void advertising_start(void)
{
	k_work_submit(&adv_work);
}


/* ------------------------------------------------------------------------
 * 3. Connection events
 * ------------------------------------------------------------------------ */

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
		return;
	}
	printk("Connected\n");
	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));
	notify_enabled = false; /* the next phone must subscribe again */
	dk_set_led_off(CON_STATUS_LED);
}

/* The phone accepted new connection parameters (see prj.conf).
 * interval is in units of 1.25 ms, timeout in units of 10 ms. */
static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			     uint16_t latency, uint16_t timeout)
{
	printk("Connection parameters: interval %u.%02u ms, latency %u, timeout %u ms\n",
	       interval * 125 / 100, interval * 125 % 100, latency, timeout * 10);
}

/* The connection object is free again: start advertising so the phone can reconnect. */
static void recycled_cb(void)
{
	advertising_start();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected,
	.disconnected = disconnected,
	.recycled     = recycled_cb,
	.le_param_updated = le_param_updated,
};


/* ------------------------------------------------------------------------
 * 4. Sow state, crush alarm and buttons
 * ------------------------------------------------------------------------ */

/* State of the sow. "lying" is written by the main loop (accelerometer) and
 * read by the button callback (scream). Both are normal threads; 32-bit
 * reads/writes on this CPU are done in one go, so no lock is needed for these
 * simple variables. */
static bool     lying;           /* true = sow lying down */
static uint32_t lying_since_ms;  /* uptime when she lay down */
static bool     crush_alarm;     /* lying + scream inside the window */
static bool     simulate_sensor_fail; /* Button 4: pretend the DHT11 read fails */

/* Called when the accelerometer sees a change of posture. */
static void set_lying(bool now_lying)
{
	if (now_lying == lying) {
		return; /* no change */
	}
	if (now_lying) {
		lying_since_ms = k_uptime_get_32(); /* set before "lying" (scream check) */
		lying = true;
		printk("Sow lying down: listening for screams for %d s\n",
		       SCREAM_WINDOW_MS / 1000);
	} else {
		lying = false;
		/* Standing up clears the alarm (the farmer has checked the crate). */
		crush_alarm = false;
		printk("Sow standing up, alarm cleared\n");
	}
	dk_set_led(LYING_LED, lying);
	dk_set_led(ALARM_LED, crush_alarm);
}

/* Called by Nordic's DK library when any button changes.
 * How it works: pressing a button causes a GPIO INTERRUPT; the library then
 * schedules a short scan in the system work queue (that also removes the
 * contact bounce) and calls this function from there, not from the interrupt.
 *   button_state: bit = 1 if that button is pressed now
 *   has_changed:  bit = 1 if that button changed since last call */
static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	/* We only react when a button is PRESSED (changed AND now pressed). */
	uint32_t pressed = button_state & has_changed;

	if (pressed & SCREAM_BUTTON) {
		uint32_t since_lying_ms = k_uptime_get_32() - lying_since_ms;

		if (lying && since_lying_ms <= SCREAM_WINDOW_MS) {
			crush_alarm = true;
			dk_set_led_on(ALARM_LED);
			printk("Scream %u ms after lying down -> CRUSH ALARM\n", since_lying_ms);
		} else {
			printk("Scream ignored (sow standing or window over)\n");
		}
	}

	if (pressed & SENSOR_FAIL_BUTTON) {
		simulate_sensor_fail = !simulate_sensor_fail;
		printk("Simulated DHT11 failure %s\n", simulate_sensor_fail ? "ON" : "OFF");
	}
}


/* ------------------------------------------------------------------------
 * 5. Real sensor: MPU-9250 accelerometer -> posture (standing / lying)
 * ------------------------------------------------------------------------ */

/* We talk to the chip directly over I2C (a few registers) instead of using
 * Zephyr's mpu9250 driver: many "MPU-9250" boards really carry an MPU-6500,
 * which that driver rejects, while these registers are the same on both.
 * Bus: i2c0 moved to SDA P1.04 / SCL P1.06 in the overlay.
 *
 * Idea: the accelerometer always measures gravity (1 g, pointing down).
 * At boot we save that direction as "standing" (so the sensor must be in its
 * standing position when the board starts). Later, if the direction of
 * gravity turns more than 60 degrees away from it, the sow is lying down. */
#define MPU_ADDR             0x68  /* AD0 pin low (default on the breakout) */
#define MPU_REG_PWR_MGMT_1   0x6B  /* write 0 to wake the chip up (it starts asleep) */
#define MPU_REG_ACCEL_XOUT_H 0x3B  /* X, Y, Z: 3 x int16, big-endian */
#define MPU_REG_WHO_AM_I     0x75  /* chip id: 0x71 MPU-9250, 0x70 MPU-6500 */

static const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static bool    accel_ok;              /* false -> posture stays "standing" */
static int32_t ref_x, ref_y, ref_z;   /* gravity at boot = standing */

/* Reads the 3 axes. Default range +-2 g: 16384 = 1 g. We divide by 16
 * (1 g = 1024) so the products in update_posture() fit in 64 bits. */
static int accel_read(int32_t *x, int32_t *y, int32_t *z)
{
	uint8_t buf[6];
	int err = i2c_burst_read(i2c, MPU_ADDR, MPU_REG_ACCEL_XOUT_H, buf, sizeof(buf));

	if (err) {
		return err;
	}
	*x = (int16_t)sys_get_be16(&buf[0]) / 16;
	*y = (int16_t)sys_get_be16(&buf[2]) / 16;
	*z = (int16_t)sys_get_be16(&buf[4]) / 16;
	return 0;
}

/* Wakes the chip up and saves the "standing" reference. */
static int accel_init(void)
{
	uint8_t who;
	int err;

	if (!device_is_ready(i2c)) {
		return -ENODEV;
	}
	err = i2c_reg_read_byte(i2c, MPU_ADDR, MPU_REG_WHO_AM_I, &who);
	if (err) {
		return err; /* nobody answered at 0x68: check wiring / AD0 */
	}
	printk("Accelerometer WHO_AM_I = 0x%02x\n", who);

	err = i2c_reg_write_byte(i2c, MPU_ADDR, MPU_REG_PWR_MGMT_1, 0x00);
	if (err) {
		return err;
	}
	k_msleep(100); /* let the first measurement finish */

	err = accel_read(&ref_x, &ref_y, &ref_z);
	if (err) {
		return err;
	}
	printk("Standing reference: x %d, y %d, z %d (1 g = 1024)\n", ref_x, ref_y, ref_z);
	return 0;
}

/* Called every 2 s: is gravity more than 60 degrees away from the reference?
 * Angle between two vectors a and r:  cos(angle) = (a . r) / (|a| |r|).
 * angle > 60  <=>  cos < 0.5  <=>  a.r <= 0  or  4 (a.r)^2 < |a|^2 |r|^2
 * (squared to avoid square roots). */
static void update_posture(void)
{
	int32_t x, y, z;

	if (!accel_ok) {
		return;
	}
	if (accel_read(&x, &y, &z)) {
		printk("Accelerometer read failed, posture unchanged\n");
		return;
	}

	int64_t dot = (int64_t)x * ref_x + (int64_t)y * ref_y + (int64_t)z * ref_z;
	int64_t a2 = (int64_t)x * x + (int64_t)y * y + (int64_t)z * z;
	int64_t r2 = (int64_t)ref_x * ref_x + (int64_t)ref_y * ref_y +
		     (int64_t)ref_z * ref_z;
	bool tilted = (dot <= 0) || (4 * dot * dot < a2 * r2);

	printk("Accel: x %d, y %d, z %d -> %s\n", x, y, z, tilted ? "lying" : "standing");
	set_lying(tilted);
}


/* ------------------------------------------------------------------------
 * 6. Real sensor: SW-420 vibration module -> water flow
 * ------------------------------------------------------------------------ */

/* The SW-420 has a small spring switch: while it vibrates, its DO pin goes
 * up and down many times. We count those pulses, like a real pulse flow meter
 * (Design) whose pulses are proportional to the flow:
 *     water (mL/min) = average pulses per 2 s window x WATER_ML_PER_PULSE
 * The average uses the last WATER_WINDOWS windows so the value (and the
 * low-water flag) does not jump up and down with every tap.
 * No vibration -> 0 mL/min -> low-water flag. Lab uses this as a proxy.
 * Module powered at 3.3 V (VDD), DO on P0.04: see the overlay in boards/. */
static const struct gpio_dt_spec vib =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), vibration_gpios);

static struct gpio_callback vib_cb;  /* registration data for the interrupt */
static atomic_t vib_pulses;          /* pulses since the last update_water() */
static bool     vib_ok;              /* false -> sensor not set up, water stays 0 */
static uint32_t last_pulses[WATER_WINDOWS]; /* pulses of the last windows */
static int      next_window;         /* where the next window is stored */
static int      water;               /* flow sent in the frame, mL/min */

/* Runs in the INTERRUPT every time DO goes to its active level. Keep it tiny:
 * only add 1. atomic_inc because the main loop reads and clears the counter. */
static void vib_pulse(const struct device *port, struct gpio_callback *cb,
		      uint32_t pins)
{
	atomic_inc(&vib_pulses);
}

/* Configures P0.04 as input with an interrupt on each pulse. */
static int vibration_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&vib)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&vib, GPIO_INPUT);
	if (err) {
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&vib, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		return err;
	}
	gpio_init_callback(&vib_cb, vib_pulse, BIT(vib.pin));
	return gpio_add_callback(vib.port, &vib_cb);
}

/* Turns the vibration interrupt off / on. Used around the DHT11 read: while
 * shaking, hundreds of interrupts break the DHT11 timing (measured 23 Nov:
 * 3 failures in a row only while shaking). The read takes ~25 ms of every
 * 2 s, so we lose only ~1 % of the pulses. */
static void vibration_pause(bool pause)
{
	if (vib_ok) {
		gpio_pin_interrupt_configure_dt(&vib, pause ? GPIO_INT_DISABLE
							    : GPIO_INT_EDGE_TO_ACTIVE);
	}
}

/* Called every 2 s: turns the pulses of the last windows into a flow. */
static void update_water(void)
{
	/* atomic_set returns the old value and leaves 0 for the next window. */
	uint32_t pulses = (uint32_t)atomic_set(&vib_pulses, 0);
	uint32_t sum = 0;

	/* Save this window over the oldest one, then add all of them. */
	last_pulses[next_window] = pulses;
	next_window = (next_window + 1) % WATER_WINDOWS;
	for (int i = 0; i < WATER_WINDOWS; i++) {
		sum += last_pulses[i];
	}

	uint32_t flow = sum * WATER_ML_PER_PULSE / WATER_WINDOWS;

	if (flow > WATER_MAX) {
		flow = WATER_MAX;
	}
	water = (int)flow;
	printk("Vibration: %u pulses (sum of last %d: %u) -> water %d mL/min\n",
	       pulses, WATER_WINDOWS, sum, water);
}


/* ------------------------------------------------------------------------
 * 7. Real sensor: DHT11 (temperature + humidity)
 * ------------------------------------------------------------------------ */

/* The DHT11 device declared in boards/nrf52840dk_nrf52840.overlay
 * (node label "dht11"). DEVICE_DT_GET gives a pointer to its driver. */
static const struct device *dht = DEVICE_DT_GET(DT_NODELABEL(dht11));

/* Reads that fail one after another before we report a sensor error.
 * Without locking interrupts, about 40 % of single reads fail (BLE interrupts
 * break the DHT11 timing; measured 4 Nov), so one failure alone is not an error. */
#define MAX_SENSOR_FAILS 3

/* Last GOOD reading, already in the frame units. */
static int16_t  temp_x100;     /* °C x 100 */
static uint16_t hum_x100;      /* %RH x 100 */
static bool     sensor_error = true;  /* flags bit2. true until the first good read */

/* Failed reads in a row. Starts "full" so that, until the first good read,
 * the frame says sensor error instead of sending 0 °C as if it were real. */
static int      fails_in_a_row = MAX_SENSOR_FAILS;

/* Statistics for the report (failure rate). */
static uint32_t total_reads;
static uint32_t total_fails;

/* Called every 2 s. Reads the DHT11 (takes about 25 ms).
 * - Read OK          -> new values, no error.
 * - 1 or 2 fails     -> keep sending the last good values, no error.
 * - 3 fails in a row -> temp = hum = 0 and bit2 set (sensor error).
 * (Decided 4 Nov, option A.) */
static void read_dht(void)
{
	struct sensor_value temp, hum;  /* value = val1 + val2 / 1 000 000 */
	int err;

	if (simulate_sensor_fail) {
		err = -EIO; /* pretend it failed (Button 4) */
	} else {
		/* 1. Ask the sensor for a new measurement (vibration interrupt
		 *    paused meanwhile, see vibration_pause()). */
		vibration_pause(true);
		err = sensor_sample_fetch(dht);
		vibration_pause(false);
	}

	if (err == 0) {
		/* 2. Get the two values out of the driver. */
		sensor_channel_get(dht, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(dht, SENSOR_CHAN_HUMIDITY, &hum);
	}

	total_reads++;

	if (err) {
		total_fails++;
		if (fails_in_a_row < MAX_SENSOR_FAILS) {
			fails_in_a_row++;
		}
		printk("DHT11 read failed (err %d), %d in a row, total %u/%u\n",
		       err, fails_in_a_row, total_fails, total_reads);

		if (fails_in_a_row >= MAX_SENSOR_FAILS) {
			sensor_error = true;
			temp_x100 = 0;
			hum_x100 = 0;
		}
		/* else: keep the last good values and the error flag as they are */
		return;
	}

	fails_in_a_row = 0;
	sensor_error = false;
	/* x100 for the frame. val2 is in millionths, so /10000 gives hundredths.
	 * (With the DHT11 the driver gives whole numbers: val2 is always 0.) */
	temp_x100 = (int16_t)(temp.val1 * 100 + temp.val2 / 10000);
	hum_x100 = (uint16_t)(hum.val1 * 100 + hum.val2 / 10000);
	printk("DHT11: %d.%02d C, %u.%02u %%RH\n", temp_x100 / 100, temp_x100 % 100,
	       hum_x100 / 100, hum_x100 % 100);
}


/* ------------------------------------------------------------------------
 * 8. Data frame (docs/protocol.md, version 1, little-endian)
 * ------------------------------------------------------------------------ */

/* Sequence number. It only goes up when a frame is really sent,
 * so a gap seen by the app means a frame lost on the BLE link (decided 4 Nov). */
static uint16_t seq;

/* Fills the 15 bytes of the frame.
 * sys_put_le16/32 write a number byte by byte, least significant byte first. */
static void build_frame(uint8_t frame[FRAME_LEN])
{
	/* Flags byte: one bit per alarm. "1 << n" is a number with only bit n set. */
	uint8_t flags = 0;
	if (crush_alarm) {
		flags |= 1 << 0;   /* bit0: crush alarm */
	}
	if (water < WATER_LOW_LIMIT) {
		flags |= 1 << 1;   /* bit1: low water */
	}
	if (sensor_error) {
		flags |= 1 << 2;   /* bit2: DHT11 read failed */
	}

	frame[0] = FRAME_VERSION;                    /* offset 0: version   */
	sys_put_le16(seq, &frame[1]);                /* offset 1: seq       */
	sys_put_le32(k_uptime_get_32(), &frame[3]);  /* offset 3: timestamp (ms since boot) */
	sys_put_le16((uint16_t)temp_x100, &frame[7]); /* offset 7: temp (signed, same bits) */
	sys_put_le16(hum_x100, &frame[9]);            /* offset 9: hum       */
	sys_put_le16((uint16_t)water, &frame[11]);   /* offset 11: water    */
	frame[13] = lying ? 1 : 0;                   /* offset 13: posture (accelerometer) */
	frame[14] = flags;                           /* offset 14: flags    */
}

/* Prints the frame in hex on the UART log (test evidence). */
static void print_frame(const uint8_t frame[FRAME_LEN])
{
	printk("Frame seq %u:", seq);
	for (int i = 0; i < FRAME_LEN; i++) {
		printk(" %02x", frame[i]);
	}
	printk("\n");
}


/* ------------------------------------------------------------------------
 * 9. main: init + loop every 2 s
 * ------------------------------------------------------------------------ */

int main(void)
{
	uint8_t frame[FRAME_LEN];
	int blink_status = 0;
	int err;

	printk("Starting SowGuard node\n");

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return 0;
	}

	/* Registers button_changed() and enables the button interrupts. */
	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Buttons init failed (err %d)\n", err);
		return 0;
	}

	/* If the driver could not start, we keep going: BLE still works and
	 * every frame will carry the sensor-error flag. */
	if (!device_is_ready(dht)) {
		printk("DHT11 device not ready\n");
	}

	/* Same idea: without the vibration sensor the flow stays at 0 (low water). */
	err = vibration_init();
	vib_ok = (err == 0);
	if (err) {
		printk("Vibration sensor init failed (err %d)\n", err);
	}

	/* Without the accelerometer the sow stays "standing" (no crush alarm). */
	err = accel_init();
	accel_ok = (err == 0);
	if (err) {
		printk("Accelerometer init failed (err %d)\n", err);
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}
	printk("Bluetooth initialized\n");

	k_work_init(&adv_work, adv_work_handler);
	advertising_start();

	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);

		/* Sensor + simulation run always, even with no phone connected. */
		read_dht();
		update_water();
		update_posture();

		if (notify_enabled) {
			build_frame(frame);

			/* NULL = send to every connected phone that enabled notifications.
			 * attrs[2] is the characteristic value (see the table above). */
			err = bt_gatt_notify(NULL, &sowguard_svc.attrs[2], frame, FRAME_LEN);
			if (err) {
				printk("Notify failed (err %d)\n", err);
			} else {
				print_frame(frame);
				seq++; /* uint16: after 65535 it wraps to 0 */
			}
		}

		k_sleep(K_MSEC(SEND_PERIOD_MS));
	}
}
