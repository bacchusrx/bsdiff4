/* 
 * bsdiff4 port for node.js (https://github.com/bacchusrx/bsdiff4)
 *
 * This port was derived from a number of sources including: the Python library
 * bsdiff4 (https://github.com/ilanschnell/bsdiff4), which was itself derived
 * from cx_bsdiff (http://cx-bsdiff.sf.net); the original BSD version, BSDiff
 * (http://www.daemonology.net/bsdiff); and the existing node.js port,
 * node-bsdiff (https://github.com/mikepb/node-bsdiff).
 *
 */

#include <node.h>
#include <node_buffer.h>
#include <v8.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <vector>

#define MIN(x, y)  (((x) < (y)) ? (x) : (y))

using namespace node;
using namespace v8;

namespace BSDiff
{
  struct Baton
  {
    uv_work_t request;

    Persistent<Function> callback;

    int error;
    std::string error_message;
    
    unsigned char *origData;
    unsigned char *newData;

    int32_t origDataLength;
    int32_t newDataLength;
    
    Persistent<Value> origHandle;
    Persistent<Value> newHandle;

    std::vector<int32_t> control;

    unsigned char *db; // diff block
    unsigned char *eb; // extra block

    int32_t dblen; // diff block length
    int32_t eblen; // extra block length

    Persistent<Value> diffHandle;
    Persistent<Value> extraHandle;
  };

  static void split(int32_t *I,int32_t *V,int32_t start,int32_t len,int32_t h)
  {
    int32_t i,j,k,x,tmp,jj,kk;

    if(len<16) {
      for(k=start;k<start+len;k+=j) {
        j=1;x=V[I[k]+h];
        for(i=1;k+i<start+len;i++) {
          if(V[I[k+i]+h]<x) {
            x=V[I[k+i]+h];
            j=0;
          };
          if(V[I[k+i]+h]==x) {
            tmp=I[k+j];I[k+j]=I[k+i];I[k+i]=tmp;
            j++;
          };
        };
        for(i=0;i<j;i++) V[I[k+i]]=k+j-1;
        if(j==1) I[k]=-1;
      };
      return;
    };

    x=V[I[start+len/2]+h];
    jj=0;kk=0;
    for(i=start;i<start+len;i++) {
      if(V[I[i]+h]<x) jj++;
      if(V[I[i]+h]==x) kk++;
    };
    jj+=start;kk+=jj;

    i=start;j=0;k=0;
    while(i<jj) {
      if(V[I[i]+h]<x) {
        i++;
      } else if(V[I[i]+h]==x) {
        tmp=I[i];I[i]=I[jj+j];I[jj+j]=tmp;
        j++;
      } else {
        tmp=I[i];I[i]=I[kk+k];I[kk+k]=tmp;
        k++;
      };
    };

    while(jj+j<kk) {
      if(V[I[jj+j]+h]==x) {
        j++;
      } else {
        tmp=I[jj+j];I[jj+j]=I[kk+k];I[kk+k]=tmp;
        k++;
      };
    };

    if(jj>start) split(I,V,start,jj-start,h);

    for(i=0;i<kk-jj;i++) V[I[jj+i]]=kk-1;
    if(jj==kk-1) I[jj]=-1;

    if(start+len>kk) split(I,V,kk,start+len-kk,h);
  }

  static void qsufsort(int32_t *I,int32_t *V,unsigned char *old,int32_t oldsize)
  {
    int32_t buckets[256];
    int32_t i,h,len;

    for(i=0;i<256;i++) buckets[i]=0;
    for(i=0;i<oldsize;i++) buckets[old[i]]++;
    for(i=1;i<256;i++) buckets[i]+=buckets[i-1];
    for(i=255;i>0;i--) buckets[i]=buckets[i-1];
    buckets[0]=0;

    for(i=0;i<oldsize;i++) I[++buckets[old[i]]]=i;
    I[0]=oldsize;
    for(i=0;i<oldsize;i++) V[i]=buckets[old[i]];
    V[oldsize]=0;
    for(i=1;i<256;i++) if(buckets[i]==buckets[i-1]+1) I[buckets[i]]=-1;
    I[0]=-1;

    for(h=1;I[0]!=-(oldsize+1);h+=h) {
      len=0;
      for(i=0;i<oldsize+1;) {
        if(I[i]<0) {
          len-=I[i];
          i-=I[i];
        } else {
          if(len) I[i-len]=-len;
          len=V[I[i]]+1-i;
          split(I,V,i,len,h);
          i+=len;
          len=0;
        };
      };
      if(len) I[i-len]=-len;
    };

    for(i=0;i<oldsize+1;i++) I[V[i]]=i;
  }

  static int32_t matchlen(unsigned char *old,int32_t oldsize,unsigned char *_new,int32_t newsize)
  {
    int32_t i;

    for(i=0;(i<oldsize)&&(i<newsize);i++)
      if(old[i]!=_new[i]) break;

    return i;
  }

  static int32_t search(int32_t *I,unsigned char *old,int32_t oldsize,
      unsigned char *_new,int32_t newsize,int32_t st,int32_t en,int32_t *pos)
  {
    int32_t x,y;

    if(en-st<2) {
      x=matchlen(old+I[st],oldsize-I[st],_new,newsize);
      y=matchlen(old+I[en],oldsize-I[en],_new,newsize);

      if(x>y) {
        *pos=I[st];
        return x;
      } else {
        *pos=I[en];
        return y;
      }
    };

    x=st+(en-st)/2;
    if(memcmp(old+I[x],_new,MIN(oldsize-I[x],newsize))<0) {
      return search(I,old,oldsize,_new,newsize,x,en,pos);
    } else {
      return search(I,old,oldsize,_new,newsize,st,x,pos);
    };
  }

  static void AsyncDiff(uv_work_t *req)
  {
    Baton *baton = static_cast<Baton*>(req->data);

    /* Input */

    unsigned char *origData = baton->origData;
    int32_t origDataLength = baton->origDataLength;

    unsigned char *newData = baton->newData;
    int32_t newDataLength = baton->newDataLength;

    /* Output */

    std::vector<int32_t> &control = baton->control;

    unsigned char *db, *eb;
    int32_t dblen = 0, eblen = 0;

    /* Algorithm */
    
    int32_t lastscan, lastpos, lastoffset, oldscore, scsc, overlap, Ss, lens;
    int32_t *I, *V, scan, pos, len, s, Sf, lenf, Sb, lenb, i;

    I = new (std::nothrow) int32_t[origDataLength + 1];
    if (I == NULL) {
      baton->error = 1;
      baton->error_message = "no memory for I";
      return;
    }
    
    V = new (std::nothrow) int32_t[newDataLength + 1];
    if (V == NULL) {
      baton->error = 1;
      baton->error_message = "no memory for V";
      delete I;
      return;
    }

    qsufsort(I, V, origData, origDataLength);
    
    delete V;
    
    db = new (std::nothrow) unsigned char[newDataLength + 1];
    if (db == NULL) {
      baton->error = 1;
      baton->error_message = "no memory for db";
      delete I;
      return;
    }

    eb = new (std::nothrow) unsigned char[newDataLength + 1];
    if (eb == NULL) {
      baton->error = 1;
      baton->error_message = "no memory for eb";
      delete I;
      delete db;
      return;
    }

    /*perform the diff*/

    scan=0;
    len=0;
    lastscan=0;
    lastpos=0;
    lastoffset=0;
    while(scan<newDataLength) {
      oldscore=0;

      for(scsc=scan+=len;scan<newDataLength;scan++) {
        len=search(I,origData,origDataLength,newData+scan,newDataLength-scan,
            0,origDataLength,&pos);

        for(;scsc<scan+len;scsc++)
          if((scsc+lastoffset<origDataLength) &&
              (origData[scsc+lastoffset] == newData[scsc]))
            oldscore++;

        if(((len==oldscore) && (len!=0)) || 
            (len>oldscore+8)) break;

        if((scan+lastoffset<origDataLength) &&
            (origData[scan+lastoffset] == newData[scan]))
          oldscore--;
      };

      if((len!=oldscore) || (scan==newDataLength)) {
        s=0;Sf=0;lenf=0;
        for(i=0;(lastscan+i<scan)&&(lastpos+i<origDataLength);) {
          if(origData[lastpos+i]==newData[lastscan+i]) s++;
          i++;
          if(s*2-i>Sf*2-lenf) { Sf=s; lenf=i; };
        };

        lenb=0;
        if(scan<newDataLength) {
          s=0;Sb=0;
          for(i=1;(scan>=lastscan+i)&&(pos>=i);i++) {
            if(origData[pos-i]==newData[scan-i]) s++;
            if(s*2-i>Sb*2-lenb) { Sb=s; lenb=i; };
          };
        };

        if(lastscan+lenf>scan-lenb) {
          overlap=(lastscan+lenf)-(scan-lenb);
          s=0;Ss=0;lens=0;
          for(i=0;i<overlap;i++) {
            if(newData[lastscan+lenf-overlap+i]==
                origData[lastpos+lenf-overlap+i]) s++;
            if(newData[scan-lenb+i]==
                origData[pos-lenb+i]) s--;
            if(s>Ss) { Ss=s; lens=i+1; };
          };

          lenf+=lens-overlap;
          lenb-=lens;
        };

        for(i=0;i<lenf;i++)
          db[dblen+i]=newData[lastscan+i]-origData[lastpos+i];
        for(i=0;i<(scan-lenb)-(lastscan+lenf);i++)
          eb[eblen+i]=newData[lastscan+lenf+i];

        dblen+=lenf;
        eblen+=(scan-lenb)-(lastscan+lenf);
        
        control.push_back(lenf);
        control.push_back((scan-lenb)-(lastscan+lenf));
        control.push_back((pos-lenb)-(lastpos+lenf));

        lastscan=scan-lenb;
        lastpos=pos-lenb;
        lastoffset=pos-scan;
      };
    };

    delete I;

    baton->db = db;
    baton->dblen = dblen;

    baton->eb = eb;
    baton->eblen = eblen;
  }

  static void DeleteMemory(char *data, void *hint) {
    delete data;
  }

  static void AfterDiff(uv_work_t *req)
  {
    HandleScope scope;

    Baton *baton = static_cast<Baton*>(req->data);

    if (baton->error) {
      Handle<Value> argv[] = { Exception::Error(String::New(baton->error_message.c_str())) };

      TryCatch tryCatch;
      baton->callback->Call(Context::GetCurrent()->Global(), 1, argv);
      if (tryCatch.HasCaught()) FatalException(tryCatch);
    }
    else {
      Local<Array> control = Array::New(baton->control.size());

      for (size_t i = 0; i < baton->control.size(); i++) {
        control->Set(static_cast<unsigned int>(i), Int32::New(baton->control[i]));
      }

      Buffer *diff = Buffer::New(reinterpret_cast<char*>(baton->db),
          static_cast<size_t>(baton->dblen), DeleteMemory, NULL);

      Buffer *extra = Buffer::New(reinterpret_cast<char*>(baton->eb), 
          static_cast<size_t>(baton->eblen), DeleteMemory, NULL);

      Handle<Value> argv[] = { Null(), control, diff->handle_, extra->handle_ };

      TryCatch tryCatch;
      baton->callback->Call(Context::GetCurrent()->Global(), 4, argv);
      if (tryCatch.HasCaught()) FatalException(tryCatch);
    }
      
    baton->origHandle.Dispose();
    baton->newHandle.Dispose();
    baton->callback.Dispose();

    delete baton;
  }

  Handle<Value> Diff(const Arguments &args)
  {
    HandleScope scope;

    if (args.Length() != 3)
      return ThrowException(Exception::TypeError(String::New("invalid arguments")));

    if (!Buffer::HasInstance(args[0]))
      return ThrowException(Exception::TypeError(String::New("args[0] not a Buffer")));

    if (!Buffer::HasInstance(args[1]))
      return ThrowException(Exception::TypeError(String::New("args[1] not a Buffer")));

    if (!args[2]->IsFunction())
      return ThrowException(Exception::TypeError(String::New("args[2] not a function")));

    Local<Object> origData = args[0]->ToObject();
    Local<Object> newData  = args[1]->ToObject();
    Local<Function> callback = Local<Function>::Cast(args[2]);

    Baton *baton = new Baton();
    baton->request.data = baton;
    
    baton->error = 0;

    baton->origData = reinterpret_cast<unsigned char*>(Buffer::Data(origData));
    baton->origDataLength = static_cast<int32_t>(Buffer::Length(origData));
    baton->origHandle = Persistent<Value>::New(origData);

    baton->newData = reinterpret_cast<unsigned char*>(Buffer::Data(newData));
    baton->newDataLength = static_cast<int32_t>(Buffer::Length(newData));
    baton->newHandle = Persistent<Value>::New(newData); 
    
    baton->callback = Persistent<Function>::New(callback);
    
    uv_queue_work(uv_default_loop(), &baton->request, AsyncDiff, AfterDiff);
    
    return Undefined();
  }

  static void AsyncPatch(uv_work_t *req)
  {
    Baton *baton = static_cast<Baton*>(req->data);

    /* Input */

    unsigned char *origData = baton->origData;
    int32_t origDataLength = baton->origDataLength;
    int32_t newDataLength = baton->newDataLength;

    std::vector<int32_t> &control = baton->control;

    unsigned char *diffBlock = baton->db;
    int32_t diffBlockLength = baton->dblen;

    unsigned char *extraBlock = baton->eb;
    int32_t extraBlockLength = baton->eblen;

    /* Output */

    unsigned char *newData;

    /* Algorithm */

    unsigned char *diffPtr, *extraPtr;
    int32_t oldpos, newpos, x, y, z, j;

    newData = new (std::nothrow) unsigned char[newDataLength + 1];
    if (newData == NULL) {
      baton->error = 1;
      baton->error_message = "no memory for newData";
      return;
    }

    oldpos = 0;
    newpos = 0;
    diffPtr = diffBlock;
    extraPtr = extraBlock;
    for (size_t i = 2; i < control.size(); i+=3) {
        x = control[i-2];
        y = control[i-1];
        z = control[i];

        /* sanity check */
        if (newpos + x > newDataLength || diffPtr + x > diffBlock + diffBlockLength || extraPtr + y > extraBlock + extraBlockLength) {
            baton->error = 1;
            baton->error_message = "corrupt patch (overflow)";
            delete newData;
            return;
        }

        memcpy(newData + newpos, diffPtr, x);
        diffPtr += x;
        for (j = 0; j < x; j++)
            if ((oldpos + j >= 0) && (oldpos + j < origDataLength))
                newData[newpos + j] += origData[oldpos + j];
        newpos += x;
        oldpos += x;
        memcpy(newData + newpos, extraPtr, y);
        extraPtr += y;
        newpos += y;
        oldpos += z;
    }

    /* sanity check */
    if (newpos != newDataLength || diffPtr != diffBlock + diffBlockLength || extraPtr != extraBlock + extraBlockLength) {
        baton->error = 1;
        baton->error_message = "corrupt patch (underflow)";
        delete newData;
        return;
    }

    baton->newData = newData;
  }

  static void AfterPatch(uv_work_t *req)
  {
    HandleScope scope;

    Baton *baton = static_cast<Baton*>(req->data);

    if (baton->error) {
      Handle<Value> argv[] = { Exception::Error(String::New(baton->error_message.c_str())) };

      TryCatch tryCatch;
      baton->callback->Call(Context::GetCurrent()->Global(), 1, argv);
      if (tryCatch.HasCaught()) FatalException(tryCatch);
    }
    else {
      Buffer *newData = Buffer::New(reinterpret_cast<char*>(baton->newData),
          static_cast<size_t>(baton->newDataLength), DeleteMemory, NULL);

      Handle<Value> argv[] = { Null(), newData->handle_ };

      TryCatch tryCatch;
      baton->callback->Call(Context::GetCurrent()->Global(), 2, argv);
      if (tryCatch.HasCaught()) FatalException(tryCatch);
    }

    baton->origHandle.Dispose();
    baton->diffHandle.Dispose();
    baton->extraHandle.Dispose();
    baton->callback.Dispose();

    delete baton;
  }

  Handle<Value> Patch(const Arguments &args)
  {
    HandleScope scope;

    if (args.Length() != 6)
      return ThrowException(Exception::TypeError(String::New("invalid arguments")));

    if (!Buffer::HasInstance(args[0]))
      return ThrowException(Exception::TypeError(String::New("args[0] not a Buffer")));

    if (!args[1]->IsNumber())
      return ThrowException(Exception::TypeError(String::New("args[1] not a Number")));

    if (!args[2]->IsArray())
      return ThrowException(Exception::TypeError(String::New("args[2] not an Array")));

    if (!Buffer::HasInstance(args[3]))
      return ThrowException(Exception::TypeError(String::New("args[3] not a Buffer")));

    if (!Buffer::HasInstance(args[4]))
      return ThrowException(Exception::TypeError(String::New("args[4] not a Buffer")));

    if (!args[5]->IsFunction())
      return ThrowException(Exception::TypeError(String::New("args[5] not a function")));
     
    Local<Object> origData = args[0]->ToObject();
    Local<Number> newDataLength = Local<Number>::Cast(args[1]);
    Local<Array> control = Local<Array>::Cast(args[2]);
    Local<Object> diffBlock = args[3]->ToObject();
    Local<Object> extraBlock = args[4]->ToObject();
    Local<Function> callback = Local<Function>::Cast(args[5]);

    Baton *baton = new Baton();
    baton->request.data = baton;

    baton->error = 0;

    baton->origData = reinterpret_cast<unsigned char*>(Buffer::Data(origData));
    baton->origDataLength = static_cast<int32_t>(Buffer::Length(origData));
    baton->origHandle = Persistent<Value>::New(origData);

    baton->newDataLength = newDataLength->Int32Value();

    for (unsigned int i = 0; i < control->Length(); i++) {
      baton->control.push_back(control->Get(i)->Int32Value());
    }

    baton->db = reinterpret_cast<unsigned char*>(Buffer::Data(diffBlock));
    baton->dblen = static_cast<int32_t>(Buffer::Length(diffBlock));
    baton->diffHandle = Persistent<Value>::New(diffBlock);

    baton->eb = reinterpret_cast<unsigned char*>(Buffer::Data(extraBlock));
    baton->eblen = static_cast<int32_t>(Buffer::Length(extraBlock));
    baton->extraHandle = Persistent<Value>::New(extraBlock);

    baton->callback = Persistent<Function>::New(callback);
   
    uv_queue_work(uv_default_loop(), &baton->request, AsyncPatch, AfterPatch);
    
    return Undefined();
  }

  void Init(Handle<Object> target)
  {
    HandleScope scope;
    NODE_SET_METHOD(target, "diff", Diff);
    NODE_SET_METHOD(target, "patch", Patch);
  }

}

NODE_MODULE(bsdiff4, BSDiff::Init)
