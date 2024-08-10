# tuya_rf
Attempt to integrate a tuya rf433 hub into esphome.

The tuya device I'm using is a Moes ufo-r2-rf, it's both an IR and RF-bridge.

It uses a CBU module (BK7231N) and an S4 RF module (which appears to be using a CMT2300A).

For more hardware details see [this forum post](https://www.elektroda.com/rtvforum/topic3975921.html).

There are several devices using the same CBU/S4 combo.

I didn't test but the IR part should work with the standard remote-receiver/remote-transmitter components (the IR receiver is on P8 and the transmitter on P7).

My code is based on the remote-transmitter component, adding the initialization sequence to put the CMT2300A in direct TX mode.

For the time being the transmitting part seems to be working, I got the codes using the original firmware and the tinytuya [RFRemoteControlDevice.py](https://github.com/jasonacox/tinytuya/blob/master/tinytuya/Contrib/RFRemoteControlDevice.py), the gencodes.py script uses the data to generate `the remote_transmitter.transmit_raw` codes.

The codes have been captured from a ceiling fan remote, the only rf remote I have.

I plan to implement the receiver, but since the CMT2300A cannot do both at the same time, the idea is to leave it always in direct RX mode, switch it to TX mode when needed and switching it back to RX mode when the code has been sent.
