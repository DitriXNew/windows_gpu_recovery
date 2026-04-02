import 'dart:io';

import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Key used to persist the counter value across GPU recovery restarts.
/// The GPU recovery plugin destroys and recreates the Flutter engine
/// (Dart VM restarts), so in-memory state is lost. SharedPreferences
/// survives because it's stored on disk.
const _kCounterKey = 'gpu_recovery_counter';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'GPU Recovery Demo',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.green),
      ),
      home: const CounterPage(),
    );
  }
}

class CounterPage extends StatefulWidget {
  const CounterPage({super.key});

  @override
  State<CounterPage> createState() => _CounterPageState();
}

class _CounterPageState extends State<CounterPage> {
  int _counter = 0;
  bool _recovered = false;
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _loadState();
  }

  /// On startup, check if we were restarted by GPU recovery.
  /// The C++ side writes a marker file next to the exe before recreation.
  /// If the marker exists, we know this is a recovery restart.
  Future<void> _loadState() async {
    final prefs = await SharedPreferences.getInstance();
    final savedCounter = prefs.getInt(_kCounterKey) ?? 0;

    // Check for recovery marker file written by flutter_window.cpp.
    final exePath = Platform.resolvedExecutable;
    final exeDir = File(exePath).parent.path;
    final markerFile = File('$exeDir/gpu_recovery.marker');
    final recovered = markerFile.existsSync();

    if (recovered) {
      // Delete marker so next normal launch doesn't show the banner.
      markerFile.deleteSync();
    }

    setState(() {
      _counter = savedCounter;
      _recovered = recovered;
      _loading = false;
    });
  }

  /// Increment counter and persist immediately.
  /// When GPU device loss happens, the engine is destroyed and recreated.
  /// The Dart VM restarts from scratch — only disk-persisted state survives.
  void _incrementCounter() {
    setState(() => _counter++);
    _persistCounter();
  }

  /// Save current counter to SharedPreferences.
  Future<void> _persistCounter() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setInt(_kCounterKey, _counter);
  }

  /// Dismiss the recovery banner.
  void _dismissBanner() {
    setState(() => _recovered = false);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        title: const Text('WITH GPU Recovery'),
      ),
      body: Column(
        children: [
          // --- Recovery banner ---
          // Shown after the engine was recreated by the GPU recovery plugin.
          // Proves that state was persisted and restored successfully.
          if (_recovered)
            MaterialBanner(
              content: Text(
                'GPU recovered. Counter restored to $_counter.',
                style: const TextStyle(fontWeight: FontWeight.bold),
              ),
              backgroundColor: Colors.green.shade100,
              actions: [
                TextButton(
                  onPressed: _dismissBanner,
                  child: const Text('DISMISS'),
                ),
              ],
            ),

          // --- Counter display ---
          Expanded(
            child: Center(
              child: _loading
                  ? const CircularProgressIndicator()
                  : Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        const Text('You have pushed the button this many times:'),
                        const SizedBox(height: 8),
                        Text(
                          '$_counter',
                          style: Theme.of(context).textTheme.displayMedium,
                        ),
                        const SizedBox(height: 32),
                        Text(
                          'Try putting the system to sleep.\n'
                          'The counter value will survive GPU recovery.',
                          textAlign: TextAlign.center,
                          style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                                color: Colors.grey,
                              ),
                        ),
                      ],
                    ),
            ),
          ),
        ],
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: _loading ? null : _incrementCounter,
        tooltip: 'Increment',
        child: const Icon(Icons.add),
      ),
    );
  }
}
