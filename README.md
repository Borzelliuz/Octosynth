# Octosynth - ESP32-Based Polyphonic Touch Synthesizer

Octosynth is an ESP32-based interactive digital synthesizer developed as an embedded systems project. The device uses capacitive touch pads, potentiometers, a TFT display, SD card storage, and real-time audio generation to create a small educational musical instrument.

The project demonstrates embedded programming, hardware-software integration, audio synthesis, user interface design, and circuit prototyping.

## Features

- ESP32-based embedded synthesizer
- One-octave capacitive touch keyboard
- 8-note polyphony
- Multiple waveform options:
  - Sine
  - Square
  - Triangle
  - Sawtooth
- TFT display menu interface
- Potentiometer-based sound control
- Standard play mode
- Tutorial mode
- Melody game mode
- Loop recording and playback
- SD card save/load support
- Real-time audio generation
- Speaker output using amplifier module
- Breadboard and circuit prototype design

## Hardware Components

- ESP32 DevKit V1
- ILI9341 SPI TFT Display
- MicroSD card slot / SD card module
- PAM8403 audio amplifier
- 8 Ohm speaker
- Copper tape capacitive touch pads
- B10K potentiometers
- Push buttons
- Breadboard
- Jumper wires
- Resistors and capacitors
- Powerbank / USB power source

## Technologies Used

- Arduino C/C++
- ESP32
- SPI Communication
- I2C Communication
- I2S / DAC Audio Concepts
- SD Card File Handling
- Capacitive Touch Input
- PWM / Timer Concepts
- KiCad
- Proteus
- Serial Monitor Debugging

## Project Structure

Octosynth/
- Octosynth_ESP32_Touch.ino
- schematics/
  - circuit_diagram.png
- images/
  - prototype_photo.jpg
- README.md
- report/
  - Octosynth_Final_Report.pdf

## How It Works

Octosynth uses the ESP32 as the main controller. Copper tape pads are connected to ESP32 touch-capable pins and work as a small musical keyboard. When the user touches a pad, the ESP32 detects the input and generates the corresponding note.

The system supports polyphonic sound generation, allowing multiple notes to be played at the same time. Different waveforms can be selected to change the sound character.

The TFT display is used to show the menu, selected mode, parameters, and user feedback. Potentiometers are used to control sound parameters such as pitch, attack, cutoff, and wobble depending on the selected mode.

The SD card system is used for saving and loading recorded loops. During recording, note events are stored with timing information and can later be played back.

## Modes

## Standard Mode

In Standard Mode, the user can freely play notes using the touch pads. Potentiometers are used to control sound parameters in real time.

## Tutorial Mode

Tutorial Mode helps the user interact with the instrument by following simple note-based guidance.

## Melody Game Mode

Melody Game Mode works like a memory-based musical game. The system shows or plays a note sequence, and the user tries to repeat it correctly.

## Recording Mode

In Recording Mode, the user can record note events and play them back later. The project also includes SD card support for saving and loading loops.

## Debugging and Testing

Serial Monitor was used during development to test and debug:

- Touch pad values
- Calibration thresholds
- Potentiometer readings
- Pitch control problems
- SD card initialization
- Loop save/load system
- Audio output behavior
- Runtime errors and crashes

## Circuit Design

The project was first developed on a breadboard and later documented using schematic design tools. KiCad and Proteus were used for circuit planning and project documentation.

Main circuit sections:

- ESP32 control unit
- Touch input section
- TFT display section
- SD card section
- Potentiometer input section
- Audio output and amplifier section
- Power distribution

## What I Learned

- ESP32 programming with Arduino C/C++
- Capacitive touch input handling
- Real-time audio generation
- TFT display menu design
- SD card read/write operations
- Embedded system debugging
- Hardware-software integration
- Circuit design and prototyping
- Working with SPI, I2C, and I2S concepts
- Preparing technical reports and project documentation

## Future Improvements

- Design a custom PCB
- Add an external I2S audio DAC
- Improve audio quality
- Add rechargeable battery support
- Add a stronger enclosure
- Add more touch pads and octave support
- Add chord and scale modes
- Add preset sound options
- Add delay and reverb effects
- Add arpeggiator mode
- Add LFO controls
- Add more filter types
- Add built-in metronome
- Improve tutorial and game modes
- Improve SD card loop management

## Authors

Alperen Çelik  
Emre Kurdoğlu  

Computer Engineering Students
