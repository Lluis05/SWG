/*
 * SowGuard node firmware — nRF52840-DK
 *
 * Based on Nordic's peripheral_lbs sample (LicenseRef-Nordic-5-Clause).
 *
 * The board is a BLE peripheral. It advertises our service, and once a phone
 * connects and enables notifications, it sends one 15-byte frame every 2 s.
 * Frame layout: docs/protocol.md (version 1).
 *
 * Step 3: posture, piglet scream, crush alarm and water flow are SIMULATED
 * with the DK buttons. Temperature/humidity are still fixed values (the
 * DHT11 comes in step 4).
 *
 * Buttons (lab simulation of the design's sensors):
 *   Button 1  sow stands up / lies down   (design: accelerometer)
 *   Button 2  piglet scream               (design: microphone)
 *   Button 3  start / stop a water drop   (design: flow meter)
 *   Button 4  free (step 4: simulate a DHT11 failure)
 * LEDs:
 *   LED1 phone connected · LED2 crush alarm · LED3 sow lying · LED4 blinks (running)
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/random/random.h>

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

#define POSTURE_BUTTON   DK_BTN1_MSK
#define SCREAM_BUTTON    DK_BTN2_MSK
#define WATER_BUTTON     DK_BTN3_MSK

#define SEND_PERIOD_MS   2000      /* one frame every 2 s (protocol.md) */
#define FRAME_LEN        15        /* bytes per frame (protocol.md) */
#define FRAME_VERSION    1

/* Simulation parameters (agreed 4 Nov). Demo values, NOT real measurements. */
#define SCREAM_WINDOW_MS   30000   /* a scream counts only up to 30 s after lying down */
#define WATER_BASE         400     /* normal flow, mL/min */
#define WATER_NOISE        15      /* random +/- variation, mL/min */
#define WATER_DROP_STEP    20      /* mL/min less every 2 s while dropping */
#define WATER_LOW_LIMIT    200     /* below this -> low-water flag */


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

/* The connection object is free again: start advertising so the phone can reconnect. */
static void recycled_cb(void)
{
	advertising_start();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected,
	.disconnected = disconnected,
	.recycled     = recycled_cb,
};


/* ------------------------------------------------------------------------
 * 4. Simulation: buttons, crush alarm and water flow
 * ------------------------------------------------------------------------ */

/* State of the simulated sow. Written by the button callback and read by the
 * main loop (both are normal threads; 32-bit reads/writes on this CPU are
 * done in one go, so no lock is needed for these simple variables). */
static bool     lying;           /* true = sow lying down */
static uint32_t lying_since_ms;  /* uptime when she lay down */
static bool     crush_alarm;     /* lying + scream inside the window */
static bool     water_dropping;  /* Button 3 started a water drop */
static int      water_base = WATER_BASE;  /* flow without noise, mL/min */
static int      water;                    /* flow sent in the frame, mL/min */

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

	if (pressed & POSTURE_BUTTON) {
		lying = !lying;
		if (lying) {
			lying_since_ms = k_uptime_get_32();
			printk("Sow lying down: listening for screams for %d s\n",
			       SCREAM_WINDOW_MS / 1000);
		} else {
			/* Standing up clears the alarm (the farmer has checked the crate). */
			crush_alarm = false;
			printk("Sow standing up, alarm cleared\n");
		}
		dk_set_led(LYING_LED, lying);
		dk_set_led(ALARM_LED, crush_alarm);
	}

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

	if (pressed & WATER_BUTTON) {
		water_dropping = !water_dropping;
		if (!water_dropping) {
			water_base = WATER_BASE; /* back to normal at once */
		}
		printk("Water drop %s\n", water_dropping ? "started" : "stopped");
	}
}

/* Called every 2 s: updates the simulated water flow. */
static void update_water(void)
{
	if (water_dropping && water_base >= WATER_DROP_STEP) {
		water_base -= WATER_DROP_STEP;
	}

	/* Random number between -WATER_NOISE and +WATER_NOISE. */
	int noise = (int)(sys_rand32_get() % (2 * WATER_NOISE + 1)) - WATER_NOISE;

	water = water_base + noise;
	if (water < 0) {
		water = 0;
	}
}


/* ------------------------------------------------------------------------
 * 5. Data frame (docs/protocol.md, version 1, little-endian)
 * ------------------------------------------------------------------------ */

/* Sequence number. It only goes up when a frame is really sent,
 * so a gap seen by the app means a frame lost on the BLE link (decided 4 Nov). */
static uint16_t seq;

/* Fills the 15 bytes of the frame.
 * sys_put_le16/32 write a number byte by byte, least significant byte first. */
static void build_frame(uint8_t frame[FRAME_LEN])
{
	/* Still fixed until step 4 (DHT11). */
	int16_t  temp = 2345;  /* 23.45 °C  (°C x 100) */
	uint16_t hum  = 5500;  /* 55.00 %RH (%RH x 100) */

	/* Flags byte: one bit per alarm. "1 << n" is a number with only bit n set. */
	uint8_t flags = 0;
	if (crush_alarm) {
		flags |= 1 << 0;   /* bit0: crush alarm */
	}
	if (water < WATER_LOW_LIMIT) {
		flags |= 1 << 1;   /* bit1: low water */
	}
	/* bit2 (sensor read failed): step 4 */

	frame[0] = FRAME_VERSION;                    /* offset 0: version   */
	sys_put_le16(seq, &frame[1]);                /* offset 1: seq       */
	sys_put_le32(k_uptime_get_32(), &frame[3]);  /* offset 3: timestamp (ms since boot) */
	sys_put_le16((uint16_t)temp, &frame[7]);     /* offset 7: temp (signed, same bits) */
	sys_put_le16(hum, &frame[9]);                /* offset 9: hum       */
	sys_put_le16((uint16_t)water, &frame[11]);   /* offset 11: water    */
	frame[13] = lying ? 1 : 0;                   /* offset 13: posture  */
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
 * 6. main: init + loop every 2 s
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

		/* The simulation runs always, even with no phone connected. */
		update_water();

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
