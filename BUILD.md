# Build Instructions

## Requirements

---

### Compilation  Requirements

Ensure your system has all the necessary packages installed:

```bash
sudo apt-get install -y --no-install-recommends make autoconf automake bison flex gcc g++ \
gawk libncurses5-dev pkg-config libconfuse-dev libssl-dev python3 python3-pip python-is-python3 \
cmake libyaml-dev scons mtools bzip2 curl git openssh-client rsync dosfstools ca-certificates
```

Additionally, install Python packages using `pip`:

```bash
pip3 install pycryptodome gmssl scons==3.1.2
```

### Source Layout

This fork tracks the SDK and component sources in one Git repository. No `repo`
installation, manifest sync, or submodule initialization is required.

## Building the Project

---

### Get the Source Code

Clone your private repository and select the firmware branch:

```bash
git clone --branch dart_control_board https://github.com/hyperle/k230_rtos_sdk.git
cd k230_rtos_sdk
# For MicroPython firmware instead:
# git switch micropython
```

Use `k230d_rtos_dart_v1_defconfig` on `dart_control_board`, or
`k230d_canmv_dart_v1_defconfig` on `micropython`. Both branches include the common
RT-Smart, MPP, U-Boot, OpenSBI, library, and model sources. The MicroPython branch
also includes CanMV, the interpreter, and its resources.

### Build for a Specific Board

1. **Download the toolchain** (only required for the first build):

   ```bash
   make dl_toolchain
   ```

2. **List all available configurations**:

   ```bash
   make list-def
   ```

3. **Select a configuration** for your board:

   ```bash
   make k230d_canmv_dart_v1_defconfig  # micropython branch
   # make k230d_rtos_dart_v1_defconfig  # dart_control_board branch
   ```

4. **Start the build process**:

   ```bash
   time make log
   ```

This process will compile the software tailored to your selected board configuration.

## How to Contribute to This Project

---

This project is open-source and welcomes contributions.
For detailed information on how to contribute, please refer to the [CONTRIBUTING.md](CONTRIBUTING.md) file.
