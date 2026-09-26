#pragma once
#include "core.h"
namespace wi {
inline CLIPFORMAT managedFileFormat(){static auto f=(CLIPFORMAT)RegisterClipboardFormatW(L"WinIsland.ManagedFile.Source.v1");return f;}
inline const GUID& managedFileIdentity(){static GUID value=[](){GUID g{};CoCreateGuid(&g);return g;}();return value;}
inline bool managedFileSource(IDataObject* object){
 FORMATETC f{managedFileFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM m{};
 if(!object||FAILED(object->GetData(&f,&m)))return false;
 bool own=false;if(m.tymed==TYMED_HGLOBAL&&GlobalSize(m.hGlobal)>=sizeof(GUID)){auto p=(GUID*)GlobalLock(m.hGlobal);if(p){own=IsEqualGUID(*p,managedFileIdentity());GlobalUnlock(m.hGlobal);}}
 ReleaseStgMedium(&m);return own;
}
}
