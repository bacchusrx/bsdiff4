var test = require('tap').test;
var bsdiff4 = require('../build/Release/bsdiff4.node');
var fs = require('fs');
var crypto = require('crypto');

test("bsdiff", function(t) {
  t.plan(7);

  var origData = fs.readFileSync('./node.0.6.13');
  var newData  = fs.readFileSync('./node.0.6.14');
  var newSha1  = crypto.createHash('sha1').update(newData).toString('hex');

  bsdiff4.diff(origData, newData, function(err, control, diff, extra) {
    t.ok(!err, "no error passed to bsdiff callback");
    t.ok(control.length > 0, "control array has data");
    t.ok(diff.length > 0, "diff block has data");
    t.ok(extra.length > 0, "extra block has data");

    bsdiff4.patch(origData, newData.length, control, diff, extra, function(err, outData) {
      t.ok(!err, "no error passed to bspatch callback");
      t.ok(outData.length > 0, "outData has data");
      
      var outSha1 = crypto.createHash('sha1').update(outData).toString('hex');
      
      t.ok(newSha1 === outSha1, "SHA1 of newData matches outData");
      t.end();
    });
  });
});
