
This is a IOT MQTT demo programe.

1. A complete sub/pub mqtt client programe using in Onenet IOT network.

2. Recv command from rmt administor and execute cmd.

3. Send back command execute result to rmt administor.

4. pub json dataset periodicly with adjustable interval.

5. auto re-connect with link tiredown.

6. running in pi zero with I2C devices, can modify simply fit to other linux environment (just change PI_MODE = 1 to PI_MODE = 0 ).

7. running statistics output.

8. support to TLS1.3.

9. some codes get from OneNet example code. 


How to compile and run....

1. modify mib_mqtt_v1.ini, get information from you iot product from  https://open.iot.10086.cn/develop/global/product/#/console

2. get/change hostid, and modify mib_mqtt_v1.ini.

3. install openssl; 
   install paho mqtt c lib, goto: 
	https://www.eclipse.org/paho/index.php?page=clients/c/index.php  and https://github.com/eclipse/paho.mqtt.c
	must configure with SSL support.

4. install minIni from https://github.com/compuphase/minIni

5. install zlog for tracing/debugging from http://hardysimpson.github.io/zlog

6. install wiringPi for PI zero can  access attached I2C device.

7. install cjson from https://github.com/DaveGamble/cJSON

8. compile in PI zero:
	gcc -c -o minIni.o  minIni.c
	gcc -g -o device_run device_run.c minIni.o -lzlog  -lpaho-mqtt3as -lcrypto -lcjson -lwiringPi -lpthread
   compile in PC linux(vmware or virtualbox):
	gcc -c -o minIni.o  minIni.c
	gcc -g -o device_run device_run.c minIni.o -lzlog  -lpaho-mqtt3as -lcrypto -lcjson -lpthread

9. download tls certificate file(just like: MQTTS-certificate.pem) and put with device_run in same directionary.  https://open.iot.10086.cn/doc/mqtt/book/device-develop/manual.html

10. start device mqtt client:
	./device_run &



