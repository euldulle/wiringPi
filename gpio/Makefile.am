sbin_PROGRAMS = gpio tcp_focuser

gpio_SOURCES = \
	gpio.c \
	readall.c

gpio_CFLAGS = \
	-I ../wiringPi \
	@EXTRA_CFLAGS@ \
	$(LIBCRYPT_CFLAGS)

gpio_LDFLAGS = \
	-L../wiringPi/.libs -lwiringPi \
	$(LIBCRYPT_LIBS) # -lgpiod

tcp_focuser_SOURCES = \
	tcp_focuser.c \
	readall.c

tcp_focuser_CFLAGS = \
	-I ../wiringPi \
	@EXTRA_CFLAGS@ \
	$(LIBCRYPT_CFLAGS)

tcp_focuser_LDFLAGS = \
	-L../wiringPi/.libs -lwiringPi \
	$(LIBCRYPT_LIBS) # -lgpiod
