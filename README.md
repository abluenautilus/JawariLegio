***Jawari Legio***

This is a port of the VCV Rack module Jawari to the Noise Engineering Legio platform. Jawari is a Tanpura machine, a model of the Indian drone instrument. On a tanpura, there are four strings which are plucked in order to create a continuous drone sound. 

**Controls**

*Strum* A trigger into this jack strums the next string. Each time you strum you will advance through the four strings in a cycle. By default they are tuned to G2, C3, C3, C2. When you change the pitch, the relative intervals between these notes stays the same. In the Indian solfege notation, this corresponds to Pa Sa Sa Sa. You can change Pa to any note of the scale using the bottom knob (see below).

*Top encoder*: Tuning. You can fine tune the frequency by tuning the encoder. If you push and turn you will tune in greater increments. You can also send v/oct pitch CV into the A jack. This signal is additive with the encoder position and determines the pitch of the second string (high Sa). 

*Middle knob*: This controls the jawari shape. The "jawari" refers to the shape of the bridge that the strings interact with to produce the characteristic buzzy sound. The specific shape of this bridge can produce different sound quality. In this module, that parameter is controlled by the middle knob and the "B" CV jack. 

*Bottom knob*: The function of this knob depends on the position of the left switch. When the left switch is in the UP position, this is a simple low-pass filter. The knob and the "C" jack control cutoff frequency. When the left switch is in the DOWN position, this knob controls the semitone of the first string. 

*Left switch* Changes the function of the bottom knob from (UP) low pass filter cutoff to (DOWN) semitone of first string. 

*Right switch* Turns the formant filters on or off. This is an additional processing layer that adds more "twang" to the strings. 

The sound is mono, so L and R output jacks deliver the same signal. 

The L and R input jacks do nothing. 

**Saving**

Any time you change the pitch or the relative semitone, these values are automatically saved and will persist through module shutdown. 