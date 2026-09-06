# Rejected start requests

If a start request is refused, the touchscreen shows a dismissible message
instead of appearing unresponsive. Check that the scale and grinder are ready
and no firmware update is active. Dismissing the message does not retry the
start or operate the motor. Web commands report an unsuccessful acknowledgement.

The host test runs the touchscreen handler with accepted and rejected manual
and profile starts. Physical screen readability remains a hardware check.
