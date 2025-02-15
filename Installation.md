# Installation

Here is a quick overview of installing the Beca/Moes thermostat hardware.

For Software installation see [Flasing.md](Flasing.md) and [Configuration.md](Configuration.md).

## ATTENTION

:warning: The devices are running at 115/230 Volt AC - which is highly dangerous.

YOU CAN BE ELECTROCUTED IF YOU DON'T KNOW WHAT YOU ARE DOING!

Ask a professional to install the devices if you are unsure how to do or if local law or your insurance forbids you to do such works!
The author of this guide is not responsilbe for any damages.

## 1. Switch Main Power off

- Switch Main Power off.
- Check if main power is off.
- Ensure no one switches on.

## 2. Check and remove old Thermostat

Open old thermostat. Check function of inputs and outputs and notice the wiring.

![install-001](docs/images/install-001.jpg)  

## 2. Remove Display-Unit

You better should remove Processor-WiFi-Unit from Relays-Body before you'are going to install.
The surface is very sensitive for scratches.

![install-002](docs/images/install-002.jpg)  

## 3. Wiring

This images are showing the **GA-Version**, which has two Relays:

1. Relay 1/Output 1 (Close) is active if powered on and the room temperature is warm enough so the valve should close
2. Relay 2/Output 2 (Open) is active if powered on and the room temperature is to cold so the valve should open to heat the room

Often you only need Output 2 (Open) and Input 3 (N) and 4 (L)

![install-003](docs/images/install-003.jpg)

### Hardware Versions GA, GB and GC

- GA - Water-Heating
  - Two Relays for opening and closing valve
  - Only one Relay will be closed at the same time
  - Closing Relay PIN 1 - PIN 3 (N or L)
  - Opening Relay PIN 2 - PIN 3 (N or L)
  - Product Spec says Max Power: 3 A (labeled is 5 A)
- GB - Electric-Heating
  - Connect Heating between PIN 1 and PIN 2
  - Product Spec says Max Power: 16 A
- GC - Water/Gas Boiler
  - One Relay - potential free (dry contact)
  - Relay on PIN 1 - PIN 2 (dry contacts)
  - Product Spec says Max Power: 3 A

Overview of Hardware Versions
![install-003b-versions](docs/images/hwversions.png)  

## 4. Screw the plate to the wall

![install-004](docs/images/install-004.jpg)  

## 5. Connect to Display-Unit

You can now remove the surface protection (pull green label).

![install-005](docs/images/install-005.jpg)  

Now you can switch Main Power on.

Then you can flash this version of WThermosta - see [Flashing.md](Flashing.md)

Then you are ready for configuration - see [Configuration.md](Configuration.md)

If tuya convert is not working you must only remove the Panel from the main device for soldering or flashing with ESP pogo adapter. Shut down main-power before removing or attaching the panel!
