# k91man

[Watch face](https://www.sensorwatch.net/) for Casio F-84W, F-<code>**91**</code>W, A158W, A159W, A163W, A164W, A171W, W-31, and W-78

Countdown to 5pm.

## Behavior

From 09:00:00 through 16:59:59, the bottom row shows the exact time remaining
until 17:00:00; at 09:00 it begins at `08:00:00`. Outside that window it behaves
as a normal clock with weekday, date, 12/24-hour mode, alarm status, and a daily
low-battery check.

Low-energy updates omit seconds and avoid ADC or buzzer work. The standard Clock
face remains the single owner of the hourly signal, and this face returns to
face 0 after Second Movement's configured inactivity timeout.

![five-o-clock](https://github.com/user-attachments/assets/716a3ecc-1748-4c35-b82e-27f1b38b1202)
