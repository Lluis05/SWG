/// One reading from the SowGuard node, already decoded.
///
/// Fields follow the Dart interface frozen in docs/protocol.md.
class SensorReading {
  final int seq; // sequence number (0..65535), used to count lost packets
  final int timestampMs; // ms since the board booted (NOT wall-clock time)
  final int waterMlMin; // water flow in mL/min (simulated in the lab)
  final double tempC; // temperature in °C (real, DHT11)
  final double humidity; // relative humidity in % (real, DHT11)
  final bool lying; // true = sow lying down (simulated with Button 1)
  final bool crushAlarm; // flags bit0: lying + piglet scream -> possible crushing
  final bool lowWater; // flags bit1: water flow below the threshold
  final bool sensorError; // flags bit2: the DHT11 read failed

  const SensorReading({
    required this.seq,
    required this.timestampMs,
    required this.waterMlMin,
    required this.tempC,
    required this.humidity,
    required this.lying,
    required this.crushAlarm,
    required this.lowWater,
    required this.sensorError,
  });

  /// Handy for logs (test evidence for the report).
  @override
  String toString() =>
      'SensorReading(seq: $seq, t: $timestampMs ms, temp: $tempC °C, '
      'hum: $humidity %, water: $waterMlMin mL/min, lying: $lying, '
      'crush: $crushAlarm, lowWater: $lowWater, sensorError: $sensorError)';
}
