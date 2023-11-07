
This is a set of tools for dumping the firmware from the EEPROM of the Anchor / Cypress microcontroller model AN2131 which was used in the USB2000 USB spectrometer sold by Ocean Optics which again was used inside the NanoDrop 1000 spectrometer.

This dumping requires only access to the USB2000 external pin header and a USB connection.

While Ocean Insight (formerly Ocean Optics) freely distribute firmware updates and instructions of re-writing the firmware to the USB2000 EEPROM, unfortunately the NanoDrop 1000 uses a modified firmware which it seems it not made available anymore (if it ever was, which is doubtful).

These devices tend to suffer from EEPROM corruption over time so using this tool it should be possible to dump the EEPROM from a healthy NanoDrop 1000 and then that firmware can be written to a NanoDrop 1000 with a corrupted firmware.

# Instructions

First, ground top left pin of USB2000 GPIO header to e.g. USB casing while plugging in the USB cable, then unground. This disables loading firmware from USB.

`lsusb` should now show:

```
Bus 002 Device 028: ID 0547:2131 Anchor Chips, Inc. AN2131 EZUSB Microcontroller
```

You'll need `fxload` to load the firmware that allows dumping the EEPROM into the USB2000's RAM:

```
sudo apt install fxload
```

Now, note the `Bus` and `Device` numbers from `lsusb` and convert them to a device path like so:

```
/dev/bus/usb/<bus_number>/<device_number
```

Then use that path for the `fxload` command to load the firmware into RAM:

```
sudo fxload -t an21  -D /dev/bus/usb/002/027 -I ./Vend_Ax/Vend_Ax.hex
```

If there are no errors, you can now run the firmware dumper.

First you need to install `pyusb`:

```
sudo pip3 install pyusb
```

You may have to change the `vendor_id` and `product_id` lines at the top of `dump_eeprom.py`.

```
sudo dump_eeprom.py dump0.bin
```

If it stalls, then hit ctrl-c and unplug the USB device, ground the top-left pin again while plugging it in and re-run the `fxload` command. Now you can resume dumping to a new file where it left off by re-running the dump command with the address where it failed as an extra parameter, e.g. if the last lime the dumper output was:

```
Reading 32 bytes from address 1024
```

Then you can continue dumping where you left of TOO A NEW FILE(!) like so:

```
sudo dump_eeprom.py dump1.bin 1024
```

Once you are done dumping you can combine the files e.g:

```
cat dump{0..10}.bin >> combined.bin
```

# Re-compiling the firmware

The development kit which runs under Windows XP is in `cypress_anchor/cy3681_ez_usb_fx2_development_kit_15.zip`.

Open the project `Vend_Ax/vend_ax.prj` project in `Keil uVision2` and hit F7 to re-compile, then load the resulting `.hex` file either with `fxload` or with the "EZ-USB Control Panel" which can be started by running `EzMr.exe`. Then click the "Download" button and select the compiled `.hex` file. Now you can actuall read the firmware directly from this GUI by changing the Value and Length fields iin the row with the "Vend Req" button. WARNING: DO NOT CHANGE THE `Dir` !!! That will write to the EEPROM!. Set the "Value" field to the address where you want to read from and "Length" to the amount of data to read. I recommend a low value like 32 first. Then click the "Vend Req" button.

# Sniffing USB communication

To develop the dumper, VirtualBox was used to run Windows XP with the "EZ-USB Control Panel" and was configured to log USB communication. To do this, first make sure the VM is running and the USB device is not attached to the VM, then run:

```
VBoxManage list usbhost
```

Find the `uuid` of the Anchor USB device, then:

```
VBoxManage controlvm "WinXP NanoDrop" usbattach <usb-uuid> --capturefile <capture_filename>
```

When done, simply detach the USB device from the VM.

The resulting capture file can be opened by Wireshark.

# About the EEPROM

The USB2000 EEPROM is a 24LC256 which has 32 kilobytes and uses two-byte addresses.

Its I2C address is `1010<3-bit-chip-select><1 for read, 0 for write>` but in the firmware code the last bit is added by the driver.

So if all chip select bits are zero (which they are on the USB2000) the address is `01010001` or 0x51.

# Attribution

USB2000 is a trademark of Ocean Insight and NanoDrop is a trademark of Thermo FIsher.

This project is not in any way afficiliated with Ocean Insight (formerly Ocean Optics) nor Thermo Fisher. 