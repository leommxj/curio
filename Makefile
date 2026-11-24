.DEFAULT_GOAL := help

PYTHON ?= python3
GO ?= go
BUILD_DIR ?= $(CURDIR)/build
FIRMWARE_BUILD_DIR ?= $(BUILD_DIR)/firmware
MODE ?= user
OUTPUT ?= $(BUILD_DIR)/screen.png

quote = '$(subst ','"'"',$(1))'
absolute = $(if $(filter /%,$(1)),$(1),$(CURDIR)/$(1))
python = $(call quote,$(PYTHON))
go = $(call quote,$(GO))
mosaico = $(python) scripts/firmware/mosaico.py
device = $(if $(DEVICE),--device-id $(call quote,$(DEVICE)))
collector_binary = $(call absolute,$(BUILD_DIR))/curio-collector
windows_dir = $(call absolute,$(BUILD_DIR))/windows
firmware_bundle = $(FIRMWARE_BUILD_DIR)/curio-system-update.irisfw

.PHONY: modules help deps deps-host configure provision firmware flash doctor status screenshot recover collector collector-windows \
        install-collector uninstall-collector dev-hub test test-collector test-core test-scripts \
        test-integration test-ui test-web

help:
	@printf '%s\n' \
	  'make deps                           Download pinned source dependencies' \
	  'make configure SSID="YourWiFi"       Create private Hub configuration' \
	  'make provision [DEVICE=id] [CONFIG=path]  Write Hub configuration over Iris' \
	  'make firmware                       Build firmware and update bundle' \
	  'make flash [DEVICE=id]               Build and install through Vibe Mode' \
	  'make flash BUNDLE=/path/file.irisfw   Install an existing bundle' \
	  'make doctor                         Check the Mosaico environment' \
	  'make status [DEVICE=id]              Read device status' \
	  'make screenshot [OUTPUT=path.png]    Capture the device screen' \
	  'make recover MAC=xx:xx:xx:xx:xx:xx   Install the official recovery image' \
	  'make collector                      Build the Collector' \
	  'make collector-windows              Build Windows amd64 Collector with setup window' \
	  'make install-collector [MODE=user]   Build and install a systemd service' \
	  'make uninstall-collector [MODE=user] Remove the service; keep configuration' \
	  'make dev-hub ARGS="..."              Run the local development Hub' \
	  'make test                           Run Go, C, scripts and integration tests' \
	  'make test-ui                        Run LVGL tests' \
	  'make test-web                       Run browser tests'

deps:
	$(python) scripts/bootstrap.py

deps-host:
	$(python) scripts/bootstrap.py --host-only

configure:
	$(python) scripts/firmware/configure.py --ssid $(call quote,$(SSID)) $(ARGS)

provision: deps
	$(python) scripts/firmware/provision.py $(if $(CONFIG),--config $(call quote,$(CONFIG))) $(device)

firmware: deps
	$(python) scripts/firmware/build.py --build-dir $(call quote,$(FIRMWARE_BUILD_DIR)) $(if $(IDF_PATH),--idf-path $(call quote,$(IDF_PATH)))

ifeq ($(strip $(BUNDLE)),)
flash: firmware
	$(mosaico) iris system-update --bundle $(call quote,$(firmware_bundle)) $(device)
else
flash: deps
	$(mosaico) iris system-update --bundle $(call quote,$(BUNDLE)) $(device)
endif

doctor: deps
	$(mosaico) doctor

status: deps
	$(mosaico) iris device-status --inventory $(device)

screenshot: deps
	$(mosaico) iris screenshot $(call quote,$(call absolute,$(OUTPUT))) $(device)

recover: deps
	$(mosaico) recover --source reviewed --hardware-mac $(call quote,$(MAC))

collector:
	@mkdir -p $(call quote,$(BUILD_DIR))
	$(go) -C collector build -buildvcs=false -o $(call quote,$(collector_binary)) .

collector-windows:
	@mkdir -p $(call quote,$(windows_dir))
	GOOS=windows GOARCH=amd64 $(go) -C collector build -buildvcs=false -o $(call quote,$(windows_dir)/curio-collector.exe) .

ifeq ($(strip $(BINARY)),)
install-collector: collector
install_binary = $(collector_binary)
else
install_binary = $(BINARY)
endif
install-collector:
	$(python) scripts/collector/service.py install --$(MODE) --binary $(call quote,$(install_binary)) $(if $(CONFIG),--config $(call quote,$(CONFIG))) $(if $(RUN_AS),--run-as $(call quote,$(RUN_AS)))

uninstall-collector:
	$(python) scripts/collector/service.py uninstall --$(MODE)

dev-hub: deps-host
	$(python) scripts/dev/hub.py $(ARGS)

test: test-collector test-core test-scripts test-integration

test-collector:
	$(go) -C collector test -race ./...
	$(go) -C collector vet ./...

test-core: deps-host
	$(MAKE) -C tests/core test
	$(MAKE) -C tests/clock test
	$(MAKE) -C tests/layout test
	$(MAKE) -C tests/alerts test
	$(MAKE) -C tests/config test
	$(MAKE) -C tests/settings test
	$(MAKE) -C tests/provision test
	$(MAKE) -C tests/battery test
	$(MAKE) -C tests/orientation test
	$(MAKE) -C tests/feedback test

test-scripts:
	$(python) -m unittest discover -s tests/scripts -v

test-integration: deps-host
	GO=$(call quote,$(GO)) $(python) -W error::ResourceWarning tests/integration/test_collector_hub.py

test-ui: deps-host
	$(MAKE) -C tests/ui test

modules:
	$(python) scripts/modules.py

test-web: modules
	$(python) -m unittest discover -s tests/web -v
