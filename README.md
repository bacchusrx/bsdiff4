# bsdiff4 port for node.js

This port was derived from a number of sources including: the Python library
bsdiff4 (https://github.com/ilanschnell/bsdiff4), which was itself derived
from cx_bsdiff (http://cx-bsdiff.sf.net); the original BSD version BSDiff
(http://www.daemonology.net/bsdiff); and the existing node.js port,
node-bsdiff (https://github.com/mikepb/node-bsdiff).

This port implements the bsdiff and bspatch algorithms, but it does not parse
or generate BSDIFF 4 patch files or provide compression. The functions exported
operate on the raw control, diff and extra blocks. Operation is asynchronous.

## Usage

```javascript
var fs = require('fs');
var bsdiff4 = require('bsdiff4');

var origData = fs.readFileSync('oldfile');
var newData  = fs.readFileSync('newfile');

bsdiff4.diff(origData, newData, function(err, control, diff, extra) {
  bsdiff4.patch(origData, newData.length, control, diff, extra, function(err, out) {
    ...
  });
});
```

## Functions

* `bsdiff4.diff(origData, newData, callback)` - generate a BSDiff patch
    * `origData` - a `Buffer` containing the original (source) data
    * `newData` - a `Buffer` containing the new (target) data
    * `callback` - a function to call once a patch has been generated
        * `err` - null on success,` an `Error` object on error
        * `control` - an `Array` containing the control block
        * `diffBlock` - a `Buffer` containing the diff block
        * `extraBlock` - a `Buffer` containing the extra block

* `bsdiff4.patch(origData, newDataLength, control, diffBlock, extraBlock, callback)` - apply a BSDiff patch
    * `origData` - a `Buffer` containing the original (source) data
    * `newDataLength` - the size of the new (target) data in bytes
    * `control` - an `Array` of integers representing the control block
    * `diffBlock` - a `Buffer` containing the diff block
    * `extraBlock` - a `Buffer` containing the extra block
    * `callback` - a function to call once a patch has been applied
        * `err` - null on success, an `Error` object on error
        * `out` - a `Buffer` containing the new (target) data

## Other versions

* [node-bsdiff](https://github.com/mikepb/node-bsdiff)
* [BSDiff](http://www.daemonology.net/bsdiff)
* [bsdiff4](https://github.com/ilanschnell/bsdiff4)
* [cx_bsdiff](http://cx-bsdiff.sf.net)
