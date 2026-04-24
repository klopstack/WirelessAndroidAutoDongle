# Building Wireless Android Auto Dongle

This repository contains a docker setup to make the build process easy.

If you choose to build without Docker, refer [the Buildroot user manual
](https://buildroot.org/downloads/manual/manual.html) for more details on dependencies and setup.

## Clone
```shell
$ git clone --recurse-submodules https://github.com/nisargjhaveri/WirelessAndroidAutoDongle
```

## Build with Docker
```shell
$ docker compose run --rm rpi4 # See docker-compose.yml for available options.
```

You can use `rpi0w`, `rpi02w`, `rpi3a` or `rpi4` to build and generate an sdcard image. Once the build is successful, it'll copy the generated sdcard image in `images/` directory.

You can also use the `bash` service for more control over the build process and experimentation.

```shell
$ docker compose run --rm bash
```

## Build with manual setup
Once you have a recursive clone, you can manually build using the following set of commands.

```shell
$ cd buildroot
$ make BR2_EXTERNAL=../aa_wireless_dongle/ O=output/rpi0w raspberrypi0w_defconfig # Change output and defconfig for your board
$ cd output/rpi0w
$ make
```

When successful, this should generate the sd card image at `images/sdcard.img` in your output directory. See the "Install and Run" instructions above to use this image.

Use one of the following defconfig for the board you intend to use:
- `raspberrypi0w_defconfig` - Raspberry Pi Zero W
- `raspberrypizero2w_defconfig` - Raspberry Pi Zero 2 W
- `raspberrypi3a_defconfig` - Raspberry Pi 3A+
- `raspberrypi4_defconfig` - Raspberry Pi 4

## OTA updates (A/B rootfs) and signing keys
The image supports A/B root filesystem OTA updates using **SWUpdate**.

### Signing keypair
SWUpdate verifies signed update bundles using a public key embedded in the image at:
- `aa_wireless_dongle/board/common/rootfs_overlay/etc/swupdate/public.pem` (device-side)

You must generate a keypair and replace the placeholder `public.pem` with your real public key. Keep the private key in your build/CI environment.

Example (RSA 3072):

```shell
# Private key (keep secret; CI/build machine)
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out swupdate_signing.key

# Public key (commit into the image / overlay as public.pem)
openssl pkey -in swupdate_signing.key -pubout -out public.pem
```

Alternatively, you can use the helper script:

```shell
./scripts/ota/gen-keypair.sh keys/
```

### Triggering an OTA update
If MQTT is enabled in `aawgd.conf`, publish a command to `<prefix>/cmd`:

```text
ota https://your-server/path/update.swu
```

The device will download the bundle to `/persist/ota/update.swu`, apply it with `swupdate`, switch the boot `root=` to the other slot, and reboot. If the new slot fails to boot/confirm within a couple of attempts it will automatically roll back.

### Creating signed update bundles (.swu)
After building an image for a board (Docker or manual), generate a signed bundle next to the produced `sdcard.img`:
- `update.swu`

From the host:

```shell
./scripts/ota/make-update-bundles.sh --board raspberrypi4 --signing-key keys/swupdate_signing.key
```

Or run the same inside the repo's Docker environment (recommended if you don't have `openssl` / `cpio` installed locally):

```shell
./scripts/ota/docker-make-update-bundles.sh --board raspberrypi4 --signing-key keys/swupdate_signing.key
```

Upload `update.swu` to your HTTP server and trigger via MQTT:
- `ota https://host/update.swu`
