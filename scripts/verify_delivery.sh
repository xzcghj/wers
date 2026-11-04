#!/bin/bash
# After files were sent to the user's computer and read back, this confirms they are the same.
status=0
for name in "$@"; do
    a=$(md5sum "/mnt/user-data/outputs/wers/$name" | cut -c1-32)
    b=$(md5sum "/mnt/user-data/uploads/Projects/WERS/$name" | cut -c1-32)
    if [ "$a" == "$b" ]; then echo "same: $name"; else echo "DIFFERENT: $name"; status=1; fi
done
exit $status
