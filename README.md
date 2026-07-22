# Pro-Edge
Hardware and software for automatic edge/orgasm detection on Arduino. Built upon night-howler/protogasm.

Protogasm hasn't been updated for a few years now, yet it remains a solid base to build upon with easy-to-use hardware and software.
The hardware remains largely unchanged, providing a ready-to-print, easy-to-solder PCB designed to be easily mounted on an Arduino UNO R3. (At least that's the hardware I tested it with—it probably works with the R4 etc. as well, though untested and without guarantees).

The new Pro-Edge software should™ be install and go on any Arduino UNO R3 based Protogasm hardware.

## Features
Pro-Edge takes all the features from Protogasm and extends them (while fixing a few bugs...).

* The previously announced (but never added) Protogasm feature of configurable vibrator ramp-up speed has been implemented.
* The Automatic Edging mode has been majorly reworked. It now features configurable smooth or abrupt drop mode and a denial or release mode.
* Release mode allows the user to have a (pre-defined) orgasm after a set number of edges are reached. It features a progress bar.
* When in release mode, the device runs an anti-cheat logic, preventing the user from accumulating edges without really putting in the work. (Not completely impossible to circumvent but pretty effective).
* There is a Variance setting to add some randomness to the set target edges, requiring more or less than the set amount while lying to the user with the progress bar.

### The Automatic Edging mode
Building upon the automatic edging mode from Protogasm, this mode now features a lot of possible customization.

#### Abrupt edging mode
Abrupt edging mode increases vibrator strength to a set maximum and keeps it there until the user reaches the set limit. It then cuts off the vibration abruptly for a defined break before starting to ramp up again. This is the basic mode featured in the original Protogasm.

#### Smooth edging mode
Smooth edging mode works slightly differently from Abrupt edging mode: it reduces the vibrator speed dynamically, depending on user arousal/clenching strength. After the arousal reading decreases for a moment, the vibration gets more intense again. Rinse and repeat.
When the user clenches quite hard (hitting the set edge detection, depending on the configured sensitivity), the device switches off the vibrator completely—just as in Abrupt edging mode—before starting the configured ramp-up again after some time.
It's quite devious and much better at keeping the user close to the edge for extended periods by giving just enough stimulation to feel tantalizingly intense. It is also easier to calibrate for extremely close edges with a near impossibility of orgasm.

#### Denial mode
In Denial mode, the cycle just continues without end, edging the user over and over without ever granting release, either using abrupt or smooth edging mode.

#### Release mode
In Release mode, the user is edged over and over, just like in Denial mode, but accumulates reached edges. After a predefined number of edges are reached, the vibrator stays on (for a predefined time) after registering an edge, granting the user a release. Progress is visualized via an indicator using the built-in LEDs.
There is a Variance setting to add some randomness to the set target edges, requiring more or less than the set amount while possibly lying to the user with the progress bar as it only shows the user defined target edges without the random variance.
Furthermore, this release can come with a twist: the release duration (the time the vibrator operates at the user-configured maximum setting) is also configurable. This allows for a ruined orgasm with a very short release duration, a full orgasm, or prolonged overstimulation (POT) with a longer duration.
When in release mode, the device runs an anti-cheat logic, preventing the user from accumulating edges without really putting in the work. (Not completely impossible to circumvent but pretty effective).


### Bugfixes (some)
* Fixed timer clock speed issue: The issue where the vibrator only operated at full speed when set to the maximum speed setting has been fixed.
* Fixed standby behavior: Resolved an issue where the device failed to properly enter standby mode or restarted upon a very long button press.
* Some more unmentioned ones by now. See changelogs for details.


## License
This work is licensed under a Creative Commons Attribution-NonCommercial 4.0 International Licence, available at http://creativecommons.org/licenses/by-nc/4.0/.

## Disclaimer
The hardware designs, schematics, source code, and documentation in this repository are provided "AS IS", without warranty of any kind, express or implied. This includes, but is not limited to, the implied warranties of merchantability, fitness for a particular purpose, and non-infringement. This project involves DIY electronics, mechanical assembly, and software integration for a device intended for personal use. Building and operating uncertified DIY hardware carries inherent dangers, including but not limited to electrical shock, burns, physical injury, and risks related to material toxicity or poor hygiene. The device has not been tested, evaluated, or approved by any regulatory health or safety agency (e.g., FDA, CE, EMA). By choosing to build, modify, or use this project, you voluntarily assume all physical and legal risks associated with its assembly and operation.

This project is strictly an experimental adult novelty item intended for educational and DIY purposes. It is absolutely not a medical device and should not be used as such. This repository and its resulting hardware are intended solely for use by consenting adults (18 years of age or older, or the age of majority in your jurisdiction).

The provided software is experimental. The author does not guarantee that the code is free from bugs, vulnerabilities, or exploits. If this device (in the future) utilizes wireless connectivity (e.g., Bluetooth, Wi-Fi, or internet networking), you acknowledge the inherent privacy and security risks associated with connecting intimate hardware to a network. The user is solely responsible for securing their own data, networks, and operational environments.

IN NO EVENT SHALL THE AUTHORS, CREATORS, CONTRIBUTORS, OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES (INCLUDING BUT NOT LIMITED TO DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES), PERSONAL INJURY, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION WITH THE REPOSITORY, THE HARDWARE, THE SOFTWARE, OR THE USE OR OTHER DEALINGS IN THE PROJECT.