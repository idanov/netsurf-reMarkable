#!/usr/bin/make -f

UID ?= $(shell id -u)
GID ?= $(shell id -g)
MAKEFILE_PATH ?= $(abspath $(lastword $(MAKEFILE_LIST)))
MAKEFILE_DIR ?= $(dir $(MAKEFILE_PATH))
BUILD_DIR ?= build
BUILD_VOLUME ?= netsurf-build
export BUILD_DIR
INSTALL_DESTINATION ?= 10.11.99.1
IMAGE_TAG ?= latest
CLANGD_CONTAINER ?= netsurf-clangd

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S), Darwin)
    USE_VOLUME_MOUNT ?= YES
else
	USE_VOLUME_MOUNT ?= NO
endif

.PHONY: help all clean build install uninstall image prepare-device copy-resources copy-binary remove-resources remove-binary clangd-build clangd-start clangd-stop check-sources test-bitmap

help:
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-20s\033[0m %s\n", $$1, $$2}'

all: help ## Print this help

test-bitmap: ## Run host-side RGB565 bitmap regression tests with sanitizers
	sh libnsfb/test/bitmap-scaling.sh

clean: ## Clean build directory, build volume and clangd container
	rm -rf $(BUILD_DIR)
	docker volume rm -f $(BUILD_VOLUME)
	docker rm -f $(CLANGD_CONTAINER)

check-sources: ## Check that the browser and framebuffer sources are present
	@if [ ! -f netsurf/Makefile ] || [ ! -f libnsfb/Makefile ]; then \
		echo "Error: netsurf/ and libnsfb/ must contain the sources from this repository."; \
		exit 1; \
	fi

ifeq ($(USE_VOLUME_MOUNT), NO)
build: check-sources ## Build the browser and framebuffer library in Docker
	docker run --rm \
	    --mount type=bind,source=$(MAKEFILE_DIR)/scripts,target=/opt/netsurf/scripts,readonly \
	    --mount type=bind,source=$(MAKEFILE_DIR)/netsurf,target=/opt/netsurf/build/netsurf \
	    --mount type=bind,source=$(MAKEFILE_DIR)/libnsfb,target=/opt/netsurf/build/libnsfb \
	    -e TARGET_WORKSPACE=/opt/netsurf/build \
	    --user=$(UID):$(GID) netsurf-build:$(IMAGE_TAG) \
	    /opt/netsurf/scripts/build.sh
else
build: check-sources ## Build in Docker with a dependency volume (default on macOS)
	$(info Using volume mount for build directory)
# Initialize the volume with PREFIX directories from the Docker image if they don't exist
	docker run --rm \
		--mount type=volume,source=$(BUILD_VOLUME),target=/opt/netsurf/build \
	    netsurf-build:$(IMAGE_TAG) \
		sh -c "if [ ! -d /opt/netsurf/build/inst-arm-remarkable-linux-gnueabihf/share ]; then \
			echo 'Initializing build volume with build system files...'; \
			cp -a /opt/netsurf/build.template/inst-* /opt/netsurf/build/ 2>/dev/null || true; \
		fi && chown -R $(UID):$(GID) /opt/netsurf/build"
	docker run --rm \
	    --mount type=bind,source=$(MAKEFILE_DIR)/scripts,target=/opt/netsurf/scripts,readonly \
	    --mount type=volume,source=$(BUILD_VOLUME),target=/opt/netsurf/build \
	    --mount type=bind,source=$(MAKEFILE_DIR)/netsurf,target=/opt/netsurf/build/netsurf \
	    --mount type=bind,source=$(MAKEFILE_DIR)/libnsfb,target=/opt/netsurf/build/libnsfb \
	    -e TARGET_WORKSPACE=/opt/netsurf/build \
	    --user=$(UID):$(GID) netsurf-build:$(IMAGE_TAG) \
	    /opt/netsurf/scripts/build.sh
endif

install: build ## Build and copy binary and resources to device (requires make image first)
	$(MAKE) copy-resources copy-binary

uninstall: remove-resources remove-binary ## Uninstall binary and resources from device

image: ## Build the Docker image that is used for building netsurf
	docker build -t netsurf-build:$(IMAGE_TAG) .

prepare-device:
	ssh root@$(INSTALL_DESTINATION) mkdir -p /home/root/.netsurf /home/root/Downloads

copy-resources: prepare-device ## Copy resources and an example configuration to device
	scp -r netsurf/frontends/framebuffer/res/. root@$(INSTALL_DESTINATION):/home/root/.netsurf/
	scp example/Choices root@$(INSTALL_DESTINATION):/home/root/.netsurf/Choices.example
	ssh root@$(INSTALL_DESTINATION) 'if [ ! -f /home/root/.netsurf/Choices ]; then cp /home/root/.netsurf/Choices.example /home/root/.netsurf/Choices; fi'

copy-binary: prepare-device ## Copy binary to device
	scp netsurf/nsfb root@$(INSTALL_DESTINATION):/home/root/.netsurf/nsfb
	ssh root@$(INSTALL_DESTINATION) chmod +x /home/root/.netsurf/nsfb

remove-resources: ## Remove resources from device
	ssh root@$(INSTALL_DESTINATION) rm -rf /home/root/.netsurf

remove-binary: ## Remove binary from device
	ssh root@$(INSTALL_DESTINATION) rm -f /home/root/.netsurf/nsfb

clangd-build: check-sources ## [Dev] Prepare local dev container with clangd and compile-commands.json
	mkdir -p $(BUILD_DIR)
	docker rm -f $(CLANGD_CONTAINER)
	docker build -t netsurf-localdev -f Dockerfile.localdev .
	docker run --rm \
		--mount type=bind,source=$(MAKEFILE_DIR)/$(BUILD_DIR),target=/opt/netsurf/build \
		--mount type=bind,source=$(MAKEFILE_DIR)/netsurf,target=/opt/netsurf/build/netsurf \
		--mount type=bind,source=$(MAKEFILE_DIR)/libnsfb,target=/opt/netsurf/build/libnsfb \
	    netsurf-localdev:latest \
		chown -R $(UID):$(GID) /opt/netsurf/build
	docker run --rm \
		--mount type=bind,source=$(MAKEFILE_DIR)/scripts,target=/opt/netsurf/scripts \
		--mount type=bind,source=$(MAKEFILE_DIR)/$(BUILD_DIR),target=/opt/netsurf/build \
		--mount type=bind,source=$(MAKEFILE_DIR)/netsurf,target=/opt/netsurf/build/netsurf \
		--mount type=bind,source=$(MAKEFILE_DIR)/libnsfb,target=/opt/netsurf/build/libnsfb \
		-e TARGET_WORKSPACE=/opt/netsurf/build \
		-e MAKE="bear --append -- make" \
		--user=$(UID):$(GID) netsurf-localdev:latest \
		sh -c "cd /opt/netsurf/build && /opt/netsurf/scripts/build.sh"

clangd-start: check-sources ## [Dev] Start the local development docker container with clangd set up
	$(info To access clangd-container, you can use scripts/clangd_docker.sh.)
	docker run --detach --name netsurf-clangd \
		--mount type=bind,source=$(MAKEFILE_DIR)/scripts,target=/opt/netsurf/scripts \
		--mount type=bind,source=$(MAKEFILE_DIR)/$(BUILD_DIR),target=/opt/netsurf/build \
		--mount type=bind,source=$(MAKEFILE_DIR)/netsurf,target=/opt/netsurf/build/netsurf \
		--mount type=bind,source=$(MAKEFILE_DIR)/libnsfb,target=/opt/netsurf/build/libnsfb \
		-p 50505:50505 \
		--user=$(UID):$(GID) netsurf-localdev:latest \
		tail -f /dev/null
# Requires sudo to be able to copy the x-tools directory recursively to host
	sudo docker cp -a netsurf-clangd:/opt/x-tools \
		$(MAKEFILE_DIR)/$(BUILD_DIR)
# Change ownership to curent user and add write permission, so we don't need sudo for later deletion
	sudo chown -R $(UID):$(GID) $(BUILD_DIR)/x-tools
	chmod -R +w $(BUILD_DIR)/x-tools

clangd-stop: ## [Dev] Stop the local development docker container
	docker rm -f netsurf-clangd
