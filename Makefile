# sudo apt-get install libusb-1.0-0-dev
# sudo apt-get install libpaho-mqtt-dev

all:
	g++ main.cpp mqtt.c -lusb-1.0 -lpaho-mqtt3c -o harmony2mqtt
