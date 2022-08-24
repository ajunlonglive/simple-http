CFLAGS = -c -g -Iinclude `python3-config --cflags --embed` \
	-I /usr/local/include/php/ \
        -I /usr/local/include/php/main/ \
        -I /usr/local/include/php/Zend/ \
        -I /usr/local/include/php/TSRM/ \

LDFLAGS = -o bin/server -lphp `python3-config --ldflags --embed`

default:
	@gcc src/main.c $(CFLAGS) -o tmp/main.o
	@gcc src/ansi-colors.c $(CFLAGS) -o tmp/ansi-colors.o
	@gcc src/util.c $(CFLAGS) -o tmp/util.o
	@gcc src/log.c $(CFLAGS) -o tmp/log.o
	@gcc tmp/*.o $(LDFLAGS)
