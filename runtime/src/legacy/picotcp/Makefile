-include ../../config.mk
-include ../../tools/kconfig/.config

CC:=$(CROSS_COMPILE)gcc 
LD:=$(CROSS_COMPILE)ld
AR:=$(CROSS_COMPILE)ar
RANLIB:=$(CROSS_COMPILE)ranlib
SIZE:=$(CROSS_COMPILE)size
STRIP_BIN:=$(CROSS_COMPILE)strip
TEST_LDFLAGS=-pthread  $(PREFIX)/*.o -lvdeplug -lm -lpcre
LIBNAME:="libpicotcp.a"
LIBBASE:=build/

PREFIX?=$(PWD)/build
DEBUG?=1
PROFILE?=0
PERF?=1
ENDIAN?=little
STRIP?=0
RTOS?=0
GENERIC?=0
PTHREAD?=0
ADDRESS_SANITIZER?=0
MUTEX?=1
TIMINGS?=0
NDEBUG?=1
SAME_THREAD_TCP_MSUS?=0

# Default compiled-in protocols
#
TCP?=1
UDP?=1
DEDOS_MSUS?=1
ETH?=1
IPV4?=1
IPV4FRAG?=1
IPV6FRAG?=0
NAT?=1
ICMP4?=1
MCAST?=1
DEVLOOP?=1
PING?=1
DHCP_CLIENT?=0
DHCP_SERVER?=0
DNS_CLIENT?=1
MDNS?=1
DNS_SD?=1
SNTP_CLIENT?=1
IPFILTER?=1
CRC?=1
OLSR?=0
SLAACV4?=1
TFTP?=1
AODV?=1
MEMORY_MANAGER?=0
MEMORY_MANAGER_PROFILING?=0
TUN?=0
TAP?=0
PCAP?=0
PPP?=1
CYASSL?=0
WOLFSSL?=0
POLARSSL?=0
#IPv6 related
IPV6?=1

EXTRA_CFLAGS+=-DPICO_COMPILE_TIME=`date +%s`
EXTRA_CFLAGS+=$(PLATFORM_CFLAGS)

CFLAGS=-I../../include/ -Isrc -Ibuild/include -Isrc -Wall -Wdeclaration-after-statement -W -Wextra -Wshadow -Wcast-qual -Wwrite-strings -Wunused-variable -Wundef -Wunused-function $(EXTRA_CFLAGS) --std=gnu99
# extra flags recommanded by TIOBE TICS framework to score an A on compiler warnings
CFLAGS+= -Wconversion
# request from Toon
CFLAGS+= -Wcast-align
CFLAGS+= -Wmissing-prototypes
CFLAGS+= -Wno-missing-field-initializers


ifeq ($(DEBUG),1)
  CFLAGS+=-ggdb
else
  ifeq ($(PERF), 1)
    CFLAGS+=-O3
  else
    CFLAGS+=-Os
  endif
endif

ifeq ($(PROFILE),1)
  CFLAGS+=-lprofiler
  OPTIONS+=-DPICO_SUPPORT_CODE_PROFILING
endif

ifeq ($(TFTP),1)
  MOD_OBJ+=$(LIBBASE)pico_strings.o $(LIBBASE)/pico_tftp.o
  OPTIONS+=-DPICO_SUPPORT_TFTP
endif

ifeq ($(AODV),1)
  MOD_OBJ+=$(LIBBASE)/pico_aodv.o
  OPTIONS+=-DPICO_SUPPORT_AODV
endif

ifeq ($(GENERIC),1)
  CFLAGS+=-DGENERIC
endif

ifeq ($(PTHREAD),1)
  CFLAGS+=-DPICO_SUPPORT_PTHREAD
endif

ifeq ($(MUTEX),1)
  CFLAGS+=-DPICO_SUPPORT_MUTEX
endif

ifeq ($(TIMINGS),1)
  OPTIONS+=-DPICO_SUPPORT_TIMINGS
endif

ifeq ($(NDEBUG),1)
  OPTIONS+=-DPICO_SUPPORT_NDEBUG
endif

ifeq ($(SAME_THREAD_TCP_MSUS),1)
  OPTIONS+=-DPICO_SUPPORT_SAME_THREAD_TCP_MSUS
endif

ifneq ($(ENDIAN),little)
  CFLAGS+=-DPICO_BIGENDIAN
endif

ifneq ($(RTOS),0)
  OPTIONS+=-DPICO_SUPPORT_RTOS
endif

ifeq ($(ARCH),cortexm4-hardfloat)
  CFLAGS+=-DCORTEX_M4_HARDFLOAT -mcpu=cortex-m4 -mthumb -mlittle-endian -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb-interwork -fsingle-precision-constant
endif

ifeq ($(ARCH),cortexm4-softfloat)
  CFLAGS+=-DCORTEX_M4_SOFTFLOAT -mcpu=cortex-m4 -mthumb -mlittle-endian -mfloat-abi=soft -mthumb-interwork
endif

ifeq ($(ARCH),cortexm3)
  CFLAGS+=-DCORTEX_M3 -mcpu=cortex-m3 -mthumb -mlittle-endian -mthumb-interwork
endif

ifeq ($(ARCH),arm9)
  CFLAGS+=-DARM9 -mcpu=arm9e -march=armv5te -gdwarf-2 -Wall -marm -mthumb-interwork -fpack-struct
endif

ifeq ($(ADDRESS_SANITIZER),1)
  TEST_LDFLAGS+=-fsanitize=address -fno-omit-frame-pointer
endif

ifeq ($(ARCH),faulty)
  CFLAGS+=-DFAULTY -DUNIT_TEST
  ifeq ($(ADDRESS_SANITIZER),1)
    CFLAGS+=-fsanitize=address
  endif
  CFLAGS+=-fno-omit-frame-pointer
  UNITS_OBJ+=test/pico_faulty.o
  TEST_OBJ+=test/pico_faulty.o
  DUMMY_EXTRA+=test/pico_faulty.o
endif

ifeq ($(ARCH),msp430)
  CFLAGS+=-DMSP430
endif

ifeq ($(ARCH),esp8266)
  CFLAGS+=-DESP8266 -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals
endif

ifeq ($(ARCH),mt7681)
  CFLAGS+=-DMT7681 -fno-builtin -ffunction-sections -fno-strict-aliasing -m16bit -mabi=2 -mbaseline=V2 -mcpu=n9 -mno-div -mel -mmw-count=8 -mno-ext-mac -mno-dx-regs
endif

ifeq ($(ARCH),pic24)
  CFLAGS+=-DPIC24 -c -mcpu=24FJ256GA106  -MMD -MF -g -omf=elf \
  -mlarge-code -mlarge-data -msmart-io=1 -msfr-warn=off
endif

ifeq ($(ARCH),atmega128)
  CFLAGS+=-Wall -mmcu=atmega128 -DAVR
endif

ifeq ($(ARCH),none)
  CFLAGS+=-DARCHNONE
endif

ifeq ($(ARCH),shared)
  CFLAGS+=-fPIC
endif

build/%.o:src/%.c deps
	$(CC) -c $(CFLAGS) -o $@ $<

CORE_OBJ= build/pico_stack.o \
          build/pico_frame.o \
          build/pico_device.o \
          build/pico_protocol.o \
          build/pico_socket.o \
          build/pico_socket_multicast.o \
          build/pico_tree.o \
          build/pico_md5.o 
          #build/control_msg_handler.o \
          #build/control_protocol.o \
          #build/control_operations.o \
          #build/chord.o \
          #build/finger_table.o \
          #build/flow_table.o \
          #build/hash_function.o \
		  #build/dedos_statistics.o
          #build/routing.o 
          #build/runtime.o
          #build/communication.o 

POSIX_OBJ+= build/pico_dev_vde.o \
            build/pico_dev_tun.o \
            build/pico_dev_tap.o \
            build/pico_dev_mock.o \
            build/pico_posix.o

ifneq ($(ETH),0)
  include rules/eth.mk
endif
ifneq ($(IPV4),0)
  include rules/ipv4.mk
endif
ifneq ($(IPV4FRAG),0)
  include rules/ipv4frag.mk
endif
ifneq ($(ICMP4),0)
  include rules/icmp4.mk
endif
ifneq ($(TCP),0)
  include rules/tcp.mk
endif
ifneq ($(UDP),0)
  include rules/udp.mk
endif
ifneq ($(MCAST),0)
  include rules/mcast.mk
  include rules/igmp.mk
  include rules/mld.mk
endif
ifneq ($(NAT),0)
  include rules/nat.mk
endif
ifneq ($(DEVLOOP),0)
  include rules/devloop.mk
endif
ifneq ($(DHCP_CLIENT),0)
  include rules/dhcp_client.mk
endif
ifneq ($(DHCP_SERVER),0)
  include rules/dhcp_server.mk
endif
ifneq ($(DNS_CLIENT),0)
  include rules/dns_client.mk
endif
ifneq ($(MDNS),0)
  include rules/mdns.mk
endif
ifneq ($(DNS_SD),0)
  include rules/dns_sd.mk
endif
ifneq ($(IPFILTER),0)
  include rules/ipfilter.mk
endif
ifneq ($(CRC),0)
  include rules/crc.mk
endif
ifneq ($(OLSR),0)
  include rules/olsr.mk
endif
ifneq ($(SLAACV4),0)
  include rules/slaacv4.mk
endif
ifneq ($(IPV6),0)
  include rules/ipv6.mk
endif
ifneq ($(MEMORY_MANAGER),0)
  include rules/memory_manager.mk
endif
ifneq ($(MEMORY_MANAGER_PROFILING),0)
  OPTIONS+=-DPICO_SUPPORT_MM_PROFILING
endif
ifneq ($(SNTP_CLIENT),0)
  include rules/sntp_client.mk
endif
ifneq ($(TUN),0)
  include rules/tun.mk
endif
ifneq ($(TAP),0)
  include rules/tap.mk
endif
ifneq ($(PCAP),0)
  include rules/pcap.mk
endif
ifneq ($(PPP),0)
  include rules/ppp.mk
endif
ifneq ($(CYASSL),0)
  include rules/cyassl.mk
endif
ifneq ($(WOLFSSL),0)
  include rules/wolfssl.mk
endif
ifneq ($(POLARSSL),0)
  include rules/polarssl.mk
endif

all: mod core lib posix

core: $(CORE_OBJ)
	#@mkdir -p $(PREFIX)/lib
	#@mv src/*.o $(PREFIX)/lib

mod: $(MOD_OBJ)
	#@mkdir -p $(PREFIX)/modules
	#@mv modules/*.o $(PREFIX)/modules || echo

posix: all $(POSIX_OBJ)
	#@mv modules/*.o $(PREFIX)/modules || echo


TEST_ELF= test/picoapp.elf
TEST6_ELF= test/picoapp6.elf


test: posix
	@mkdir -p $(PREFIX)/test/
	@make -C test/examples PREFIX=$(PREFIX)
	@echo -e "\t[CC] picoapp.o"
	@$(CC) -c -o $(PREFIX)/examples/picoapp.o test/picoapp.c $(CFLAGS) -Itest/examples
	@echo -e "\t[LD] $@"
	@$(CC) -g -o $(TEST_ELF) -I include -I modules -I $(PREFIX)/include -Wl,--start-group $(TEST_LDFLAGS) $(TEST_OBJ) $(PREFIX)/examples/*.o -Wl,--end-group -lssl -lcrypto
	@mv test/*.elf $(PREFIX)/test
	@install $(PREFIX)/$(TEST_ELF) $(PREFIX)/$(TEST6_ELF)

tst: test

$(PREFIX)/include/pico_defines.h:
	#@mkdir -p $(PREFIX)/lib
	@mkdir -p $(PREFIX)/include
	@bash ./mkdeps.sh $(PREFIX) $(OPTIONS)


deps: $(PREFIX)/include/pico_defines.h



lib: mod core
	#@cp -f include/*.h $(PREFIX)/include
	#@cp -fa include/arch $(PREFIX)/include
	#@cp -f modules/*.h $(PREFIX)/include
	@cp -f src/*.h $(PREFIX)/include
	@cp -rf src/arch $(PREFIX)/include/arch
	#@echo -e "\t[AR] $(PREFIX)/lib/$(LIBNAME)"
	#@$(AR) cru $(PREFIX)/lib/$(LIBNAME) $(PREFIX)/modules/*.o $(PREFIX)/lib/*.o \
	#  || $(AR) cru $(PREFIX)/lib/$(LIBNAME) $(PREFIX)/lib/*.o
	#@echo -e "\t[RANLIB] $(PREFIX)/lib/$(LIBNAME)"
	#@$(RANLIB) $(PREFIX)/lib/$(LIBNAME)
	#@test $(STRIP) -eq 1 && (echo -e "\t[STRIP] $(PREFIX)/lib/$(LIBNAME)" \
    # && $(STRIP_BIN) $(PREFIX)/lib/$(LIBNAME)) \
    # || echo -e "\t[KEEP SYMBOLS] $(PREFIX)/lib/$(LIBNAME)"
	#@echo -e "\t[LIBSIZE] `du -b $(PREFIX)/lib/$(LIBNAME)`"
	#@echo -e "`size -t $(PREFIX)/lib/$(LIBNAME)`"

loop: mod core
	mkdir -p $(PREFIX)/test
	@$(CC) -c -o $(PREFIX)/modules/pico_dev_loop.o modules/pico_dev_loop.c $(CFLAGS)
	@$(CC) -c -o $(PREFIX)/loop_ping.o test/loop_ping.c $(CFLAGS) -ggdb

units: mod core lib $(UNITS_OBJ) $(MOD_OBJ)
	@echo -e "\n\t[UNIT TESTS SUITE]"
	@mkdir -p $(PREFIX)/test
	@echo -e "\t[CC] units.o"
	@$(CC) -g -c -o $(PREFIX)/test/units.o test/units.c $(CFLAGS) -I stack -I modules -I includes -I test/unit -DUNIT_TEST
	@echo -e "\t[LD] $(PREFIX)/test/units"
	@$(CC) -o $(PREFIX)/test/units $(CFLAGS) $(PREFIX)/test/units.o -lcheck -lm -pthread -lrt \
		$(UNITS_OBJ) $(PREFIX)/modules/pico_aodv.o \
		$(PREFIX)/modules/pico_fragments.o
	@$(CC) -o $(PREFIX)/test/modunit_pico_protocol.elf $(CFLAGS) -I. test/unit/modunit_pico_protocol.c stack/pico_tree.c -lcheck -lm -pthread -lrt $(UNITS_OBJ)
	@$(CC) -o $(PREFIX)/test/modunit_pico_frame.elf $(CFLAGS) -I. test/unit/modunit_pico_frame.c stack/pico_tree.c -lcheck -lm -pthread -lrt $(UNITS_OBJ)
	@$(CC) -o $(PREFIX)/test/modunit_seq.elf $(CFLAGS) -I. test/unit/modunit_seq.c -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_tcp.elf $(CFLAGS) -I. test/unit/modunit_pico_tcp.c -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_dns_client.elf $(CFLAGS) -I. test/unit/modunit_pico_dns_client.c -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_dns_common.elf $(CFLAGS) -I. test/unit/modunit_pico_dns_common.c -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_mdns.elf $(CFLAGS) -I. test/unit/modunit_pico_mdns.c -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_dns_sd.elf $(CFLAGS) -I. test/unit/modunit_pico_dns_sd.c -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_dev_loop.elf $(CFLAGS) -I. test/unit/modunit_pico_dev_loop.c -lcheck -lm -pthread -lrt $(UNITS_OBJ)
	@$(CC) -o $(PREFIX)/test/modunit_ipv6_nd.elf $(CFLAGS) -I. test/unit/modunit_pico_ipv6_nd.c -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_pico_stack.elf $(CFLAGS) -I. test/unit/modunit_pico_stack.c -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_tftp.elf $(CFLAGS) -I. test/unit/modunit_pico_tftp.c  -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_sntp_client.elf $(CFLAGS) -I. test/unit/modunit_pico_sntp_client.c -lcheck -lm -pthread -lrt $(UNITS_OBJ)
	@$(CC) -o $(PREFIX)/test/modunit_ipfilter.elf $(CFLAGS) -I. test/unit/modunit_pico_ipfilter.c stack/pico_tree.c -lcheck -lm -pthread -lrt $(UNITS_OBJ)
	@$(CC) -o $(PREFIX)/test/modunit_aodv.elf $(CFLAGS) -I. test/unit/modunit_pico_aodv.c  -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_fragments.elf $(CFLAGS) -I. test/unit/modunit_pico_fragments.c  -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_queue.elf $(CFLAGS) -I. test/unit/modunit_queue.c  -lcheck -lm -pthread -lrt $(UNITS_OBJ)
	@$(CC) -o $(PREFIX)/test/modunit_dev_ppp.elf $(CFLAGS) -I. test/unit/modunit_pico_dev_ppp.c  -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_mld.elf $(CFLAGS) -I. test/unit/modunit_pico_mld.c  -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_igmp.elf $(CFLAGS) -I. test/unit/modunit_pico_igmp.c  -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a
	@$(CC) -o $(PREFIX)/test/modunit_hotplug_detection.elf $(CFLAGS) -I. test/unit/modunit_pico_hotplug_detection.c  -lcheck -lm -pthread -lrt $(UNITS_OBJ) $(PREFIX)/lib/libpicotcp.a

devunits: mod core lib
	@echo -e "\n\t[UNIT TESTS SUITE: device drivers]"
	@mkdir -p $(PREFIX)/test/unit/device/
	@echo -e "\t[CC] picotcp_mock.o"
	@$(CC) -c -o $(PREFIX)/test/unit/device/picotcp_mock.o $(CFLAGS) -I stack -I modules -I includes -I test/unit test/unit/device/picotcp_mock.c
	@$(CC) -c -o $(PREFIX)/test/unit/device/unit_dev_vde.o $(CFLAGS) -I stack -I modules -I includes -I test/unit test/unit/device/unit_dev_vde.c
	@echo -e "\t[LD] $(PREFIX)/test/devunits"
	@$(CC) -o $(PREFIX)/test/devunits $(CFLAGS) -I stack $(PREFIX)/test/unit/device/*.o -lcheck -lm -pthread -lrt

units_mm: mod core lib
	@echo -e "\n\t[UNIT TESTS SUITE]"
	@mkdir -p $(PREFIX)/test
	@echo -e "\t[CC] units_mm.o"
	@$(CC) -c -o $(PREFIX)/test/units_mm.o test/unit/unit_mem_manager.c $(CFLAGS) -I stack -I modules -I includes -I test/unit
	@echo -e "\t[LD] $(PREFIX)/test/units"
	@$(CC) -o $(PREFIX)/test/units_mm $(CFLAGS) $(PREFIX)/test/units_mm.o -lcheck -lm -pthread -lrt


clean:
	@echo -e "\t[CLEAN] $(PREFIX)/"
	@rm -rf $(PREFIX) tags

mbed:
	@echo -e "\t[Creating PicoTCP.zip]"
	@rm -f PicoTCP.zip
	@cp include/pico_socket.h include/socket.tmp
	@echo "#define MBED\n" > include/mbed.tmp
	@cat include/mbed.tmp include/socket.tmp > include/pico_socket.h
	@zip -0 PicoTCP.zip -r include modules stack -x include/arch/ include/arch/* include/pico_config.h include/*.tmp modules/pico_dev_*
	@rm include/pico_socket.h include/mbed.tmp
	@mv include/socket.tmp include/pico_socket.h


style:
	@find . -iname "*.[c|h]" | xargs -x uncrustify --replace -l C -c uncrustify.cfg || true
	@find . -iname "*unc-backup*" |xargs -x rm || true

dummy: mod core lib $(DUMMY_EXTRA)
	@echo testing configuration...
	@$(CC) -c -o test/dummy.o test/dummy.c $(CFLAGS)
	@$(CC) -o dummy test/dummy.o $(DUMMY_EXTRA) $(PREFIX)/lib/libpicotcp.a $(LDFLAGS) $(CFLAGS)
	@echo done.
	@rm -f test/dummy.o dummy

ppptest: test/ppp.c lib
	gcc -ggdb -c -o ppp.o test/ppp.c -I build/include/ -I build/modules/ $(CFLAGS)
	gcc -o ppp ppp.o build/lib/libpicotcp.a $(LDFLAGS) $(CFLAGS)
	rm -f ppp.o


FORCE:
