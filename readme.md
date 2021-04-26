# Data Display using ESP32 and Hub75 LED Display

Various data will be displayed on a 32x64 Hub75 LED Display.

Code starts the RTC module,
connects to local wifi,
displays time & data,
and gets new weather data every 15 minutes.

# To Do

* decide on weather data to display
* arrange data to display on LEDs
* add air pollution, pm2.5 (or10?) for smoke season https://openweathermap.org/api/air-pollution
* add covid (see parameters.h)
* use NTP?
* add retries to get data and/or parse data?
* Fix RTC initializiation
