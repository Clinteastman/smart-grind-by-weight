# Older settings forms and queued saves

Older web forms may omit the panel-off switch or delay. An omitted field now
keeps the value present when the queued save executes, not the value present
when the browser submitted it. This prevents an older form queued behind a
newer form from reverting that newer form's panel-off choices.

Explicit values still replace the setting and are range-checked on submission.
The regression test queues both forms before applying either, and exercises
each panel-off field omitted independently as well as both omitted together.
