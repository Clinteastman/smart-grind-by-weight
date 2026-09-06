# Stored history validation

The advertised stored-session count and Bluetooth export list both include only
canonical session files that pass the same schema, metadata and length checks.
Truncated files, unsupported formats, malformed names and directories do not
appear as available downloads. Invalid files are not deleted by counting them.

The host regression runs the production count, list and validation methods
against valid, truncated and unsupported-schema files. This does not detect
arbitrary payload corruption: schema 2 has no payload checksum.
