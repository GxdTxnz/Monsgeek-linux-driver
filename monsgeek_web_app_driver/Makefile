# make - собрать драйвер
# make test - собрать и запустить тесты
# make install - установить драйвер + правило udev + сервис (sudo)
# make uninstall - удалить
# make deb - собрать deb-пакет
# make test-deb - установить пакет в docker с чистым debian:13 для проверки
# make clean - удалить файлы сборки

TARGET := monsgeek-bridge
BUILD := build
GEN := $(BUILD)/gen

CXX ?= g++
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++20 -Wall -Wextra -MMD -MP -Isrc -I$(GEN) -Ithird_party
LIBS := $(shell pkg-config --libs protobuf libudev) -lpthread

PROTO := proto/driver.proto
PB_CC := $(GEN)/driver.pb.cc
PB_H := $(GEN)/driver.pb.h
PB_OBJ := $(GEN)/driver.pb.o

LIB_SRCS := $(shell find src -name '*.cpp' ! -path src/main.cpp)
LIB_OBJS := $(LIB_SRCS:%.cpp=$(BUILD)/%.o) $(PB_OBJ)

TEST_SRCS := $(wildcard tests/test_*.cpp)
TEST_BINS := $(TEST_SRCS:tests/%.cpp=$(BUILD)/tests/%)

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
UDEV_DIR ?= /etc/udev/rules.d
USER_UNIT_DIR := $(PREFIX)/lib/systemd/user
UDEV_RULES := $(BUILD)/70-monsgeek.rules
UNIT := $(BUILD)/monsgeek-bridge.service

VERSION ?= 1.0.0
DIST := dist
DOCKER ?= docker
NFPM_IMAGE ?= docker.io/goreleaser/nfpm:v2.43.0
DEB := $(DIST)/$(TARGET)_$(VERSION)-1_amd64.deb
PKG_LIBS := -Wl,-Bstatic $(shell pkg-config --libs protobuf) -lz -Wl,-Bdynamic -ludev -lpthread -static-libstdc++ -static-libgcc

.PHONY: all test clean install uninstall deb test-deb

all: $(TARGET)

$(TARGET): $(BUILD)/src/main.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(PB_CC) $(PB_H): $(PROTO)
	mkdir -p $(GEN)
	protoc --proto_path=proto --cpp_out=$(GEN) $(PROTO)

$(PB_OBJ): $(PB_CC)
	$(CXX) $(CXXFLAGS) -Wno-unused-parameter -c -o $@ $<

$(BUILD)/%.o: %.cpp | $(PB_H)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/tests/%: tests/%.cpp $(LIB_OBJS) | $(PB_H)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) '-DTESTS_DIR="$(CURDIR)/tests"' -o $@ $< $(LIB_OBJS) $(LIBS)

test: $(TEST_BINS) $(UDEV_RULES)
	@fail=0; for t in $(TEST_BINS); do echo "== $$t"; ./$$t || fail=1; done; \
	echo "== $(UDEV_RULES)"; \
	if grep -q 'ATTRS{idVendor}=="3151", ATTRS{idProduct}=="5030"' $(UDEV_RULES); then echo "OK      udev rule for 3151:5030"; \
	else echo "FAILED  no udev rule for 3151:5030"; fail=1; fi; exit $$fail

$(UDEV_RULES): src/hid/support_table.cpp
	mkdir -p $(BUILD)
	echo '# доступ к клавам MonsGeek без sudo для monsgeek-bridge' > $@
	grep -oE '\{0x[0-9A-Fa-f]{4}, *0x[0-9A-Fa-f]{4}' $< | sed 's/[{ ]//g; s/0x//g' | tr 'A-F' 'a-f' | sort -u | \
		sed 's/\(.*\),\(.*\)/KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="\1", ATTRS{idProduct}=="\2", TAG+="uaccess"/' >> $@

$(UNIT): packaging/monsgeek-bridge.service.in
	mkdir -p $(BUILD)
	sed 's|@BINDIR@|$(BINDIR)|' $< > $@

install: $(TARGET) $(UDEV_RULES) $(UNIT)
	install -D -m 755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	install -D -m 644 $(UDEV_RULES) "$(DESTDIR)$(UDEV_DIR)/70-monsgeek.rules"
	install -D -m 644 $(UNIT) "$(DESTDIR)$(USER_UNIT_DIR)/monsgeek-bridge.service"
	@if [ -z "$(DESTDIR)" ]; then udevadm control --reload && udevadm trigger --subsystem-match=hidraw; fi
	@echo "готово. включить мост: systemctl --user daemon-reload && systemctl --user enable --now monsgeek-bridge"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)" "$(DESTDIR)$(UDEV_DIR)/70-monsgeek.rules" \
		"$(DESTDIR)$(USER_UNIT_DIR)/monsgeek-bridge.service"
	@if [ -z "$(DESTDIR)" ]; then udevadm control --reload; fi
	@echo "удалено. если сервис был включен: systemctl --user disable --now monsgeek-bridge"

$(DIST)/$(TARGET): $(BUILD)/src/main.o $(LIB_OBJS)
	mkdir -p $(DIST)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(PKG_LIBS)

$(DIST)/70-monsgeek.rules: $(UDEV_RULES)
	mkdir -p $(DIST)
	cp $< $@

$(DIST)/monsgeek-bridge.service: packaging/monsgeek-bridge.service.in
	mkdir -p $(DIST)
	sed 's|@BINDIR@|/usr/bin|' $< > $@

$(DEB): $(DIST)/$(TARGET) $(DIST)/70-monsgeek.rules $(DIST)/monsgeek-bridge.service packaging/nfpm.yaml \
		packaging/postinst.sh packaging/prerm.sh packaging/postrm.sh README.md
	$(DOCKER) run --rm -u $$(id -u):$$(id -g) -e VERSION=$(VERSION) -v "$(CURDIR):/work" -w /work \
		$(NFPM_IMAGE) package -f packaging/nfpm.yaml -p deb -t $(DIST)/

deb: $(DEB)

test-deb: $(DEB)
	packaging/test-deb.sh $(DEB)

clean:
	rm -rf $(BUILD) $(DIST) $(TARGET)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
