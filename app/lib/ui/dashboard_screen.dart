import 'dart:async';

import 'package:flutter/material.dart';

import '../models/packet_stats.dart';
import '../models/sensor_reading.dart';
import '../sources/ble_source.dart';
import '../sources/mock_source.dart';
import '../sources/reading_source.dart';
import 'app_colors.dart';

/// Main (and only) screen: live values of the node + alarms.
///
/// It only uses the ReadingSource interface, so it works the same with
/// fake data (MockSource) and with the real board (BleSource).
class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  ReadingSource? _source;
  StreamSubscription<SensorReading>? _readingSub;
  StreamSubscription<bool>? _connectedSub;

  bool _live = false; // false = simulated data, true = real board
  bool _connected = false;
  SensorReading? _last; // last reading received (null = none yet)
  PacketStats _stats = PacketStats();

  @override
  void initState() {
    super.initState();
    _useSource(live: false); // start with simulated data
  }

  @override
  void dispose() {
    _readingSub?.cancel();
    _connectedSub?.cancel();
    _source?.stop();
    super.dispose();
  }

  /// Stops the current data source and starts the other one.
  Future<void> _useSource({required bool live}) async {
    // 1. Stop the old source.
    await _readingSub?.cancel();
    await _connectedSub?.cancel();
    await _source?.stop();
    if (!mounted) return;

    // 2. Create the new one and reset the screen.
    final ReadingSource source = live ? BleSource() : MockSource();
    setState(() {
      _live = live;
      _source = source;
      _connected = false;
      _last = null;
      _stats = PacketStats();
    });

    // 3. Listen BEFORE start(), so we do not miss the first events.
    _readingSub = source.readings.listen((reading) {
      setState(() {
        _last = reading;
        _stats.add(reading.seq);
      });
    });
    _connectedSub = source.connected.listen((isConnected) {
      setState(() => _connected = isConnected);
    });

    // 4. Go.
    await source.start();
  }

  @override
  Widget build(BuildContext context) {
    final reading = _last;

    return Scaffold(
      backgroundColor: AppColors.background,
      appBar: AppBar(
        title: const Text('SowGuard'),
        backgroundColor: AppColors.barnGreen,
        foregroundColor: Colors.white,
        actions: [
          // Switch between simulated data and the real board.
          const Center(
            child: Text('Real board', style: TextStyle(color: Colors.white)),
          ),
          Switch(
            value: _live,
            onChanged: (value) => _useSource(live: value),
          ),
          const SizedBox(width: 8),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _ConnectionRow(live: _live, connected: _connected),
          const SizedBox(height: 12),

          // Red banner: the most important thing on the screen.
          if (reading != null && reading.crushAlarm)
            const _AlertBanner(
              color: AppColors.alarmRed,
              icon: Icons.warning_amber_rounded,
              title: 'Possible piglet crushing',
              message:
                  'The sow lay down and a scream was detected. Check the crate now.',
            ),

          // Amber banner: water intake dropped (early illness sign).
          if (reading != null && reading.lowWater)
            const _AlertBanner(
              color: AppColors.straw,
              icon: Icons.water_drop_outlined,
              title: 'Water intake dropped',
              message: 'Flow is below normal. Check the drinker and the sow.',
            ),

          if (reading == null)
            const Padding(
              padding: EdgeInsets.symmetric(vertical: 32),
              child: Center(child: Text('Waiting for the first reading…')),
            )
          else
            _ReadingGrid(reading: reading),

          const SizedBox(height: 20),
          _LinkStats(stats: _stats, lastSeq: reading?.seq),
        ],
      ),
    );
  }
}

/// One line telling where the data comes from and if we are connected.
class _ConnectionRow extends StatelessWidget {
  final bool live;
  final bool connected;

  const _ConnectionRow({required this.live, required this.connected});

  @override
  Widget build(BuildContext context) {
    final String text;
    final IconData icon;
    if (!live) {
      text = 'Simulated data (no board)';
      icon = Icons.science_outlined;
    } else if (connected) {
      text = 'Connected to the SowGuard node';
      icon = Icons.bluetooth_connected;
    } else {
      text = 'Searching for the SowGuard node…';
      icon = Icons.bluetooth_searching;
    }
    final color = (!live || connected) ? AppColors.barnGreen : Colors.grey;

    return Row(
      children: [
        Icon(icon, color: color),
        const SizedBox(width: 8),
        Expanded(child: Text(text)),
      ],
    );
  }
}

/// Coloured banner for alarms and warnings.
class _AlertBanner extends StatelessWidget {
  final Color color;
  final IconData icon;
  final String title;
  final String message;

  const _AlertBanner({
    required this.color,
    required this.icon,
    required this.title,
    required this.message,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: color,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, color: Colors.white, size: 32),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 18,
                    fontWeight: FontWeight.bold,
                  ),
                ),
                const SizedBox(height: 4),
                Text(message, style: const TextStyle(color: Colors.white)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// The four live values in a 2 x 2 grid.
class _ReadingGrid extends StatelessWidget {
  final SensorReading reading;

  const _ReadingGrid({required this.reading});

  @override
  Widget build(BuildContext context) {
    // If the DHT11 failed, its numbers are meaningless: show the error instead.
    final temp = reading.sensorError
        ? 'Sensor error'
        : '${reading.tempC.toStringAsFixed(1)} °C';
    final hum = reading.sensorError
        ? 'Sensor error'
        : '${reading.humidity.toStringAsFixed(0)} %';

    return Column(
      children: [
        Row(
          children: [
            Expanded(
              child: _ValueTile(
                icon: Icons.thermostat,
                label: 'Temperature',
                value: temp,
                warning: reading.sensorError,
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: _ValueTile(
                icon: Icons.opacity,
                label: 'Humidity',
                value: hum,
                warning: reading.sensorError,
              ),
            ),
          ],
        ),
        const SizedBox(height: 12),
        Row(
          children: [
            Expanded(
              child: _ValueTile(
                icon: Icons.water_drop,
                label: 'Water flow',
                value: '${reading.waterMlMin} mL/min',
                warning: reading.lowWater,
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: _ValueTile(
                icon: Icons.pets,
                label: 'Sow posture',
                value: reading.lying ? 'Lying down' : 'Standing',
              ),
            ),
          ],
        ),
      ],
    );
  }
}

/// One value with its icon and name. Turns amber when [warning] is true.
class _ValueTile extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final bool warning;

  const _ValueTile({
    required this.icon,
    required this.label,
    required this.value,
    this.warning = false,
  });

  @override
  Widget build(BuildContext context) {
    final color = warning ? AppColors.straw : AppColors.barnGreen;

    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color, width: 1.5),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, size: 18, color: color),
              const SizedBox(width: 6),
              Flexible(
                child: Text(
                  label,
                  overflow: TextOverflow.ellipsis,
                  style: Theme.of(context).textTheme.labelLarge,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Text(
            value,
            style: Theme.of(context)
                .textTheme
                .titleLarge
                ?.copyWith(color: color, fontWeight: FontWeight.w600),
          ),
        ],
      ),
    );
  }
}

/// Link quality numbers, used as evidence in the BLE tests of the report.
class _LinkStats extends StatelessWidget {
  final PacketStats stats;
  final int? lastSeq;

  const _LinkStats({required this.stats, required this.lastSeq});

  @override
  Widget build(BuildContext context) {
    final style = Theme.of(context).textTheme.bodySmall;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('Link quality', style: Theme.of(context).textTheme.labelLarge),
        const SizedBox(height: 4),
        Text('Packets received: ${stats.received}', style: style),
        Text(
          'Packets lost: ${stats.lost} '
          '(${stats.lossPercent.toStringAsFixed(1)} %)',
          style: style,
        ),
        Text('Last seq: ${lastSeq ?? '-'}', style: style),
      ],
    );
  }
}
