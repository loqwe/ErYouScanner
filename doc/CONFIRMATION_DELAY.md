# Configurable Login Confirmation Delay

The existing main window now has a seconds input, `spinConfirmDelay`, between
the scan buttons and the existing checkboxes. It accepts 0.0 to 60.0 seconds in
0.1-second increments. No upstream PR theme or layout redesign is used.

- Manual confirmation: clicking Login starts the configured delay.
- Automatic second confirmation: recognizing the QR starts the same delay.
- A value of 0 preserves immediate confirmation.
- The active scan button becomes Cancel Confirmation during the delay.
- Cancellation, closing the window or replacing the timer drops pending work.
- Confirmation is sent only after the elapsed monotonic time reaches the delay.
- Once the login request starts, both scan buttons stay disabled until a result.
- Large delays may exceed the QR service's validity period; the existing failure
  result is still reported and the application does not silently retry.

The setting is saved as the numeric `confirm_delay_seconds` key in the existing
configuration. Old configurations without this key default to 0. The original
user configuration is not edited by the source build or isolated smoke tests.

Both screen and stream workers publish one confirmation event to the UI rather
than bypassing the delay for automatic login. The UI timer never blocks its
event loop. The tracked confirmation task waits for the scanner to finish before
reading its QR state; the window drains that task before destroying the scanner.

## Tests

```powershell
python tests/test_confirmation_delay.py
cmake -S tests/confirmation_delay -B build/confirmation-delay -A x64 -DCMAKE_PREFIX_PATH="<Qt 6.8.0 msvc2022_64>"
cmake --build build/confirmation-delay --config Release
ctest --test-dir build/confirmation-delay -C Release --output-on-failure
```

Runtime tests cover fractional timing, lower/upper bounds, non-finite input,
cancel, replacement and destruction. The Windows Qt UI test renders the real
UIC-generated window without showing an interactive window or accessing
accounts. It validates the input range, default, fractional editing and layout.
The timing test also passes 10 consecutive runs.
