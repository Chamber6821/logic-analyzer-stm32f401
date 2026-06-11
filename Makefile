PRESET?=Debug

GDB_PORT=3333
RTT_PORT=9090
OPENOCD = openocd -f openocd/openocd.cfg
OPENOCD_UPLOAD = $(OPENOCD) -f openocd/upload.cfg
OPENOCD_DEBUG = $(OPENOCD) -c "set GDB_PORT $(GDB_PORT); set RTT_PORT $(RTT_PORT)" -f openocd/debug.cfg
OPENOCD_ERASE_DATA = $(OPENOCD) -f openocd/erase-data.cfg

SYMBOLS_FILE = build/firmware.elf

BLOLA = ./build/blola.py

CONNECT_GDB = gdb --symbols=$(SYMBOLS_FILE) -ex 'target extended-remote :$(GDB_PORT)'
CONNECT_RTT = nc --recv-only 127.0.0.1 $(RTT_PORT) | $(BLOLA) log

WAIT_TCP_PORT = while ! nc -z 127.0.0.1 $(1); do sleep 0.1; done

PREPROCESSED_FILES = $(wildcard build/preprocessed/**/*.i) $(wildcard build/preprocessed/**/*.ii)

configure:
	cmake -DCMAKE_COLOR_DIAGNOSTICS=ON --preset $(PRESET)

auto-configure: Makefile CMakeLists.txt CMakePresets.json
	make configure

build: auto-configure
	cmake --build --preset $(PRESET) --color=always
	make collect

collect:
	$(BLOLA) collect -s src $(PREPROCESSED_FILES)

upload: build
	$(OPENOCD_UPLOAD)

gdb:
	make openocd-debug-deamon-start
	$(call WAIT_TCP_PORT,$(GDB_PORT))
	$(CONNECT_GDB); make openocd-debug-deamon-stop

rtt:
	make openocd-debug-deamon-start
	$(call WAIT_TCP_PORT,$(RTT_PORT))
	$(CONNECT_RTT); make openocd-debug-deamon-stop

send:
	make openocd-debug-deamon-start
	$(call WAIT_TCP_PORT,$(RTT_PORT))
	nc --send-only 127.0.0.1 $(RTT_PORT); make openocd-debug-deamon-stop

erase-data:
	$(OPENOCD_ERASE_DATA)


openocd-debug-deamon-start:
	nohup $(OPENOCD_DEBUG) > build/openocd.log &

openocd-debug-deamon-stop:
	pkill openocd
