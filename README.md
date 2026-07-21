# Pro-Edge
Hardware and software for automatic edge/orgasm detection on Arduino. Built upon night-howler/protogasm.

Protogasm hasn't been updated for a few years now, yet it remains a solid base to build upon with easy-to-use hardware and software.
The hardware remains largely unchanged, providing a ready-to-print, easy-to-solder PCB designed to be easily mounted on an Arduino UNO R3. (At least that's the hardware I tested it with—it probably works with the R4 etc. as well, though untested and without guarantees).

## Features
Pro-Edge takes all the features from Protogasm and extends them (while fixing a few bugs...).

* The previously announced (but never added) Protogasm feature of configurable vibrator ramp-up speed has been implemented.
* Two additional automatic modes have been added, including various configurable settings/customization options.

### Smooth automatic edging mode (Depletion)
Building upon the automatic edging mode from Protogasm (now called 'Abrupt automatic edging mode'), this mode works largely the same, but with a critical difference: it reduces the vibrator speed dynamically, depending on user arousal/clenching strength. After the arousal reading decreases for a moment, the vibration gets more intense again. Rinse and repeat. When the user clenches quite hard (hitting the set edge detection, depending on the configured sensitivity), the device switches off the vibrator completely—just as in Protogasm's 'Abrupt edging mode'—but only for a few seconds before starting the configured ramp-up.

It's quite devious and much better at keeping the user close to the edge for extended periods by giving just enough stimulation to feel tantalizingly intense. It is also easier to calibrate for extremely close edges with a near impossibility of orgasm.
Like the Abrupt edging mode, this is a denial-only mode, unless you manually modify detection sensitivity to allow for an orgasm.

### Automatic release mode
This mode expands upon the 'Smooth automatic edging mode' with additional features.
It has mode-specific settings (accessible by switching to this mode (magenta) and then holding the button for one second).

In this mode, the device edges the user using the smooth mode, reducing vibrator speed as the edge approaches. Additionally, it allows an eventual release—but only after a user-configured number of edges (2–48, in increments of 2). Furthermore, this release can come with a twist: the release duration (the time the vibrator operates at the user-configured maximum setting) is also configurable. This allows for a ruined orgasm with a very short release duration, a full orgasm, or prolonged overstimulation (POT) with a longer duration (2–48 seconds, in increments of 2 seconds).

Progress is visualized via an indicator using the built-in LEDs.
For an added surprise, a variance or random factor can be set, adding or subtracting required edges while leaving the user in the dark.

(See the manual for detailed descriptions)

### Bugfixes (some)
* Fixed timer clock speed issue: The issue where the vibrator only operated at full speed when set to the maximum speed setting has been fixed.
* Fixed standby behavior: Resolved an issue where the device failed to properly enter standby mode or restarted upon a very long button press.


## License
This work is licensed under a Creative Commons Attribution-NonCommercial 4.0 International Licence, available at http://creativecommons.org/licenses/by-nc/4.0/.

## Disclaimer
The hardware designs, schematics, source code, and documentation in this repository are provided "AS IS", without warranty of any kind, express or implied. This includes, but is not limited to, the implied warranties of merchantability, fitness for a particular purpose, and non-infringement. This project involves DIY electronics, mechanical assembly, and software integration for a device intended for personal use. Building and operating uncertified DIY hardware carries inherent dangers, including but not limited to electrical shock, burns, physical injury, and risks related to material toxicity or poor hygiene. The device has not been tested, evaluated, or approved by any regulatory health or safety agency (e.g., FDA, CE, EMA). By choosing to build, modify, or use this project, you voluntarily assume all physical and legal risks associated with its assembly and operation.

This project is strictly an experimental adult novelty item intended for educational and DIY purposes. It is absolutely not a medical device and should not be used as such. This repository and its resulting hardware are intended solely for use by consenting adults (18 years of age or older, or the age of majority in your jurisdiction).

The provided software is experimental. The author does not guarantee that the code is free from bugs, vulnerabilities, or exploits. If this device (in the future) utilizes wireless connectivity (e.g., Bluetooth, Wi-Fi, or internet networking), you acknowledge the inherent privacy and security risks associated with connecting intimate hardware to a network. The user is solely responsible for securing their own data, networks, and operational environments.

IN NO EVENT SHALL THE AUTHORS, CREATORS, CONTRIBUTORS, OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES (INCLUDING BUT NOT LIMITED TO DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES), PERSONAL INJURY, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION WITH THE REPOSITORY, THE HARDWARE, THE SOFTWARE, OR THE USE OR OTHER DEALINGS IN THE PROJECT.