# Scale disconnect history

A weight grind stopped because fresh scale readings disappeared records
`SCALE_ERROR`, not `TIMEOUT`. The existing error screen still says
“Scale disconnected”. Reconnecting the scale does not restart the motor.

The binary session layout is unchanged. Termination-reason value 4 identifies
this failure; the existing values 0–3 and 255 retain their meanings. The text
result is also preserved in web history and Bluetooth exports. Old report
tools may label the new numeric reason UNKNOWN until updated.
