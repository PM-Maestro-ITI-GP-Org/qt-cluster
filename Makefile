QT6_DIR := ../QT/qt6-qnx-libs/output_dir
QNX800_DIR := ../../qnx800

.PHONY: all native qnx rebuild clean

all: qnx

native:
	cmake -S . -B build -DQt6_DIR=$(QT6_DIR)/host_qt/lib/cmake/Qt6
	cmake --build build

qnx:
	@[ -n "$(QNX_HOST)" ] || [ -f "$(QNX800_DIR)/qnxsdp-env.sh" ] || { echo "QNX SDP not found at $(QNX800_DIR)/qnxsdp-env.sh"; exit 1; }
	@bash -c 'set -e; \
		[ -n "$$QNX_HOST" ] || . "$(QNX800_DIR)/qnxsdp-env.sh"; \
		cmake -S . -B build_qnx \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_TOOLCHAIN_FILE=$(QT6_DIR)/lib/cmake/Qt6/qt.toolchain.cmake \
			-DQt6_DIR=$(QT6_DIR)/lib/cmake/Qt6 \
			-DQNX_LIB_DIR=$(QNX800_DIR)/target/qnx/aarch64le \
			-DFONT_SOURCE_DIR=/usr/share/fonts; \
		cmake --build build_qnx'

rebuild: clean qnx

clean:
	rm -rf build build_qnx
