/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 *   Pierre Phaneuf <pp@ludusdesign.com>
 */
  
#define DEBUG_XMLENCODING
#define XMLENCODING_PEEKBYTES 64

#include "nsParser.h"
#include "nsIContentSink.h" 
#include "nsString.h"
#include "nsCRT.h" 
#include "nsScanner.h"
#include "prenv.h"  //this is here for debug reasons...
#include "plstr.h"
#include "nsIParserFilter.h"
#include "nshtmlpars.h"
#include "CNavDTD.h"
#include "nsWellFormedDTD.h"
#include "nsViewSourceHTML.h" 
#include "nsHTMLContentSinkStream.h" //this is here so we can get a null sink, which really should be gotten from nsICOntentSink.h
#include "nsIStringStream.h"
#include "nsIChannel.h"
#include "nsIProgressEventSink.h"
#include "nsIBufferInputStream.h"

//#define rickgdebug 

 
static NS_DEFINE_IID(kISupportsIID, NS_ISUPPORTS_IID);                 
static NS_DEFINE_IID(kClassIID, NS_PARSER_IID); 
static NS_DEFINE_IID(kIParserIID, NS_IPARSER_IID);
static NS_DEFINE_IID(kIStreamListenerIID, NS_ISTREAMLISTENER_IID);

static const char* kNullURL = "Error: Null URL given";
static const char* kOnStartNotCalled = "Error: OnStartRequest() must be called before OnDataAvailable()";
static const char* kBadListenerInit  = "Error: Parser's IStreamListener API was not setup correctly in constructor.";

//-------------------------------------------------------------------
 

class CDTDDeallocator: public nsDequeFunctor{
public:
  virtual void* operator()(void* anObject) {
    nsIDTD* aDTD =(nsIDTD*)anObject;
    NS_RELEASE(aDTD);
    return 0;
  }
};

//-------------------------------------------------------------------

class CDTDFinder: public nsDequeFunctor{
public:
  CDTDFinder(nsIDTD* aDTD) {
    mTargetDTD=aDTD;
  }
  virtual ~CDTDFinder() {
  }
  virtual void* operator()(void* anObject) {
    nsIDTD* theDTD=(nsIDTD*)anObject;
    if(theDTD->GetMostDerivedIID().Equals(mTargetDTD->GetMostDerivedIID()))
      return anObject;
    return 0;
  }
  nsIDTD* mTargetDTD;
};

//-------------------------------------------------------------------

class CSharedParserObjects {
public:

  CSharedParserObjects() : mDTDDeque(0) {

    //Note: To cut down on startup time/overhead, we defer the construction of non-html DTD's. 

    nsIDTD* theDTD;
    NS_NewNavHTMLDTD(&theDTD);    //do this as the default HTML DTD...
    mDTDDeque.Push(theDTD);

    mHasViewSourceDTD=PR_FALSE;
    mHasXMLDTD=PR_FALSE;
  }

  ~CSharedParserObjects() {
    CDTDDeallocator theDeallocator;
    mDTDDeque.ForEach(theDeallocator);  //release all the DTD's
  }

  void RegisterDTD(nsIDTD* aDTD){
    if(aDTD) {
      NS_ADDREF(aDTD);
      CDTDFinder theFinder(aDTD);
      if(!mDTDDeque.FirstThat(theFinder)) {
        nsIDTD* theDTD;
        aDTD->CreateNewInstance(&theDTD);
        mDTDDeque.Push(theDTD);
      }
      NS_RELEASE(aDTD);
    }
  }
  
  nsDeque mDTDDeque;
  PRBool  mHasViewSourceDTD;  //this allows us to defer construction of this object.
  PRBool  mHasXMLDTD;         //also defer XML dtd construction
};

static CSharedParserObjects* gSharedParserObjects=0;


//-------------------------------------------------------------------------

/**********************************************************************************
  This class is used as an interface between an external agent (like the DOM) and
  the parser. It will contain a stack full of tagnames, which is used in our
  parser/paste API's.
 **********************************************************************************/

class nsTagStack : public nsITagStack {
public:
  nsTagStack() : nsITagStack(), mTags(0) {
  }

  virtual ~nsTagStack() {
  }

  virtual void Push(PRUnichar* aTag){
    mTags.Push(aTag);
  }
  
  virtual PRUnichar*  Pop(void){
    PRUnichar* result=(PRUnichar*)mTags.Pop();
    return result;
  }
  
  virtual PRUnichar*  TagAt(PRUint32 anIndex){
    PRUnichar* result=0;
    if(anIndex<(PRUint32)mTags.GetSize())
      result=(PRUnichar*)mTags.ObjectAt(anIndex);
    return result;
  }

  virtual PRUint32    GetSize(void){
    return mTags.GetSize();
  }

  nsDeque mTags;  //will hold a deque of prunichars...
};

CSharedParserObjects& GetSharedObjects() {
  if (!gSharedParserObjects) {
    gSharedParserObjects = new CSharedParserObjects();
  }
  return *gSharedParserObjects;
}

/** 
 *  This gets called when the htmlparser module is shutdown.
 *   
 *  @update  gess 01/04/99
 */
void nsParser::FreeSharedObjects(void) {
  if (gSharedParserObjects) {
    delete gSharedParserObjects;
    gSharedParserObjects=0;
  }
}

/** 
 *  default constructor
 *   
 *  @update  gess 01/04/99
 *  @param   
 *  @return   
 */
nsParser::nsParser(nsITokenObserver* anObserver) : mCommand(""), mUnusedInput("") , mCharset("ISO-8859-1") {
  NS_INIT_REFCNT();
  mParserFilter = 0;
  mObserver = 0;
  mProgressEventSink = nsnull;
  mSink=0;
  mParserContext=0;
  mTokenObserver=anObserver;
  mStreamStatus=0;
  mDTDVerification=PR_FALSE;
  mCharsetSource=kCharsetUninitialized;
  mInternalState=NS_OK;
  mObserversEnabled=PR_TRUE;

  MOZ_TIMER_DEBUGLOG(("Reset: Parse Time: nsParser::nsParser(), this=%p\n", this));
  MOZ_TIMER_RESET(mParseTime);  
  MOZ_TIMER_RESET(mDTDTime);  
  MOZ_TIMER_RESET(mTokenizeTime);
}

 
/**
 *  Default destructor
 *  
 *  @update  gess 01/04/99
 *  @param   
 *  @return  
 */
nsParser::~nsParser() {
  NS_IF_RELEASE(mObserver);
  NS_IF_RELEASE(mProgressEventSink);
  NS_IF_RELEASE(mSink);

  //don't forget to add code here to delete 
  //what may be several contexts...
  delete mParserContext;
}


NS_IMPL_ADDREF(nsParser)
NS_IMPL_RELEASE(nsParser)
//NS_IMPL_ISUPPORTS(nsParser,NS_IHTML_HTMLPARSER_IID)


/**
 *  This method gets called as part of our COM-like interfaces.
 *  Its purpose is to create an interface to parser object
 *  of some type.
 *  
 *  @update   gess 01/04/99
 *  @param    nsIID  id of object to discover
 *  @param    aInstancePtr ptr to newly discovered interface
 *  @return   NS_xxx result code
 */
nsresult nsParser::QueryInterface(const nsIID& aIID, void** aInstancePtr)  
{                                                                        
  if (NULL == aInstancePtr) {                                            
    return NS_ERROR_NULL_POINTER;                                        
  }                                                                      

  if(aIID.Equals(kISupportsIID))    {  //do IUnknown...
    *aInstancePtr = (nsIParser*)(this);                                        
  }
  else if(aIID.Equals(kIParserIID)) {  //do IParser base class...
    *aInstancePtr = (nsIParser*)(this);                                        
  }
  else if(aIID.Equals(NS_GET_IID(nsIProgressEventSink))) {
    *aInstancePtr = (nsIStreamListener*)(this);                                        
  }
  else if(aIID.Equals(NS_GET_IID(nsIStreamObserver))) {
    *aInstancePtr = (nsIStreamObserver*)(this);                                        
  }
  else if(aIID.Equals(NS_GET_IID(nsIStreamListener))) {
    *aInstancePtr = (nsIStreamListener*)(this);                                        
  }
  else if(aIID.Equals(kClassIID)) {  //do this class...
    *aInstancePtr = (nsParser*)(this);                                        
  }                 
  else {
    *aInstancePtr=0;
    return NS_NOINTERFACE;
  }
  NS_ADDREF_THIS();
  return NS_OK;                                                        
}


/**
 * 
 * @update	gess 01/04/99
 * @param 
 * @return
 */
nsIParserFilter * nsParser::SetParserFilter(nsIParserFilter * aFilter)
{
  nsIParserFilter* old=mParserFilter;
  if(old)
    NS_RELEASE(old);
  if(aFilter) {
    mParserFilter=aFilter;
    NS_ADDREF(aFilter);
  }
  return old;
}


/**
 *  Call this method once you've created a parser, and want to instruct it
 *  about the command which caused the parser to be constructed. For example,
 *  this allows us to select a DTD which can do, say, view-source.
 *  
 *  @update  gess 01/04/99
 *  @param   aContentSink -- ptr to content sink that will receive output
 *  @return	 ptr to previously set contentsink (usually null)  
 */
void nsParser::SetCommand(const char* aCommand){
  mCommand=aCommand;
}



/**
 *  Call this method once you've created a parser, and want to instruct it
 *  about what charset to load
 *  
 *  @update  ftang 4/23/99
 *  @param   aCharset- the charest of a document
 *  @param   aCharsetSource- the soure of the chares
 *  @return	 nada
 */
void nsParser::SetDocumentCharset(nsString& aCharset, nsCharsetSource aCharsetSource){
  mCharset = aCharset;
  mCharsetSource = aCharsetSource; 
}

/**
 *  This method gets called in order to set the content
 *  sink for this parser to dump nodes to.
 *  
 *  @update  gess 01/04/99
 *  @param   nsIContentSink interface for node receiver
 *  @return  
 */
nsIContentSink* nsParser::SetContentSink(nsIContentSink* aSink) {
  NS_PRECONDITION(0!=aSink,"sink cannot be null!");
  nsIContentSink* old=mSink;

  NS_IF_RELEASE(old);
  if(aSink) {
    mSink=aSink;
    NS_ADDREF(aSink);
    mSink->SetParser(this);
  }
  return old;
}

/**
 * retrive the sink set into the parser 
 * @update	gess5/11/98
 * @param   aSink is the new sink to be used by parser
 * @return  old sink, or NULL
 */
nsIContentSink* nsParser::GetContentSink(void){
  return mSink;
}

/**
 *  Call this method when you want to
 *  register your dynamic DTD's with the parser.
 *  
 *  @update  gess 01/04/99
 *  @param   aDTD  is the object to be registered.
 *  @return  nothing.
 */
void nsParser::RegisterDTD(nsIDTD* aDTD){
  CSharedParserObjects& theShare=GetSharedObjects();
  theShare.RegisterDTD(aDTD);
}

/**
 *  Retrieve scanner from topmost parsecontext
 *  
 *  @update  gess 01/04/99
 *  @return  ptr to internal scanner
 */
nsScanner* nsParser::GetScanner(void){
  if(mParserContext)
    return mParserContext->mScanner;
  return 0;
}


/**
 *  Retrieve parsemode from topmost parser context
 *  
 *  @update  gess 01/04/99
 *  @return  parsemode
 */
eParseMode nsParser::GetParseMode(void){
  if(mParserContext)
    return mParserContext->mParseMode;
  return eParseMode_unknown;
}



/**
 *  
 *  
 *  @update  gess 5/13/98
 *  @param   
 *  @return  
 */
static
PRBool FindSuitableDTD( CParserContext& aParserContext,nsString& aCommand,nsString& aBuffer) {
  
  //Let's start by trying the defaultDTD, if one exists...
  if(aParserContext.mDTD)
    if(aParserContext.mDTD->CanParse(aParserContext.mSourceType,aCommand,aBuffer,0))
      return PR_TRUE;

  CSharedParserObjects& gSharedObjects=GetSharedObjects();

  aParserContext.mAutoDetectStatus=eUnknownDetect;
  PRInt32 theDTDIndex=0;
  nsIDTD* theBestDTD=0;
  nsIDTD* theDTD=0;
  PRBool  thePrimaryFound=PR_FALSE;

  while((theDTDIndex<=gSharedObjects.mDTDDeque.GetSize()) && (aParserContext.mAutoDetectStatus!=ePrimaryDetect)){
    theDTD=(nsIDTD*)gSharedObjects.mDTDDeque.ObjectAt(theDTDIndex++);
    if(theDTD) {
      aParserContext.mAutoDetectStatus=theDTD->CanParse(aParserContext.mSourceType,aCommand,aBuffer,0);
      if(eValidDetect==aParserContext.mAutoDetectStatus){
        theBestDTD=theDTD;
      }
      else if(ePrimaryDetect==aParserContext.mAutoDetectStatus) {  
        theBestDTD=theDTD;
        thePrimaryFound=PR_TRUE;
      }
    }
    if((theDTDIndex==gSharedObjects.mDTDDeque.GetSize()) && (!thePrimaryFound)) {
      if(!gSharedObjects.mHasXMLDTD) {
        NS_NewWellFormed_DTD(&theDTD);  //do this to view XML files...
        gSharedObjects.mDTDDeque.Push(theDTD);
        gSharedObjects.mHasXMLDTD=PR_TRUE;
      }
      else if(!gSharedObjects.mHasViewSourceDTD) {
        NS_NewViewSourceHTML(&theDTD);  //do this so all non-html files can be viewed...
        gSharedObjects.mDTDDeque.Push(theDTD);
        gSharedObjects.mHasViewSourceDTD=PR_TRUE;
      }
    }
  }

  if(theBestDTD) {
    theBestDTD->CreateNewInstance(&aParserContext.mDTD);
    return PR_TRUE;
  }
  return PR_FALSE;
}


/**
 *  This is called when it's time to find out 
 *  what mode the parser/DTD should run for this document.
 *  (Each parsercontext can have it's own mode).
 *  
 *  @update  gess 5/13/98
 *  @return  parsermode (define in nsIParser.h)
 */
static
eParseMode DetermineParseMode(nsParser& aParser) {
  const char* theModeStr= PR_GetEnv("PARSE_MODE");
  const char* other="other";
  
  eParseMode result=eParseMode_unknown;
  nsScanner* theScanner=aParser.GetScanner();
  if(theScanner){
    nsAutoString theBufCopy;
    nsString& theBuffer=theScanner->GetBuffer();
    theBuffer.Left(theBufCopy,125);
    PRInt32 theIndex=theBufCopy.Find("<!");
    theIndex=(theIndex!=kNotFound)? theIndex=theBufCopy.Find("DOCTYPE",PR_TRUE,2):kNotFound;

    if(kNotFound<theIndex) {
      //good, we found "DOCTYPE" -- now go find it's end delimiter '>'
      theBufCopy.StripWhitespace();
      PRInt32 theSubIndex=theBufCopy.FindChar(kGreaterThan,theIndex+1);
      theBufCopy.Truncate(theSubIndex);
      theSubIndex=theBufCopy.Find("-//W3C//DTD",PR_TRUE,theIndex+8);
      if(kNotFound<theSubIndex) {
        if(kNotFound<(theSubIndex=theBufCopy.Find("HTML4.0",PR_TRUE,theSubIndex+11))) {
          PRUnichar num=theBufCopy.CharAt(theSubIndex+7);
          if(num > '0' && num < '9') {
            result=eParseMode_noquirks; // XXX - investigate this more.
          }
          else if((theBufCopy.Find("TRANSITIONAL",PR_TRUE,theSubIndex+7)>kNotFound)||
             (theBufCopy.Find("FRAMESET",PR_TRUE,theSubIndex+7)>kNotFound)    ||
             (theBufCopy.Find("LATIN1", PR_TRUE,theSubIndex+7) >kNotFound)    ||
             (theBufCopy.Find("SYMBOLS",PR_TRUE,theSubIndex+7) >kNotFound)    ||
             (theBufCopy.Find("SPECIAL",PR_TRUE,theSubIndex+7) >kNotFound))
            result=eParseMode_quirks; // XXX -HACK- Set the appropriate mode.
          else
            result=eParseMode_noquirks;
        }
        else if(kNotFound<(theSubIndex=theBufCopy.Find("XHTML",PR_TRUE,theSubIndex+11))) {
          if((theBufCopy.Find("TRANSITIONAL",PR_TRUE,theSubIndex)>kNotFound)||
             (theBufCopy.Find("STRICT",PR_TRUE,theSubIndex)   >kNotFound)   ||
             (theBufCopy.Find("FRAMESET",PR_TRUE,theSubIndex) >kNotFound))
            result=eParseMode_noquirks;
          else
            result=eParseMode_quirks;
        }
      }
      else if(kNotFound<(theSubIndex=theBufCopy.Find("ISO/IEC15445:1999",PR_TRUE,theIndex+8))) {
        theSubIndex=theBufCopy.Find("HTML",PR_TRUE,theSubIndex+18);
        if(kNotFound==theSubIndex)
          theSubIndex=theBufCopy.Find("HYPERTEXTMARKUPLANGUAGE",PR_TRUE,theSubIndex+18);
        result=eParseMode_noquirks;
      }
    }
    else if(kNotFound<(theIndex=theBufCopy.Find("?XML",PR_TRUE))) {
        result=eParseMode_noquirks;
    }
    else {
      theIndex=theBufCopy.Find("NOQUIRKS",PR_TRUE);
      if(kNotFound<theIndex) {
        result=eParseMode_noquirks;
      }
    }
  }

  if(theModeStr) 
    if(0==nsCRT::strcasecmp(other,theModeStr))
      return eParseMode_other;    
    return (eParseMode_unknown==result)? eParseMode_quirks:result;
}


/**
 * This gets called just prior to the model actually
 * being constructed. It's important to make this the
 * last thing that happens right before parsing, so we
 * can delay until the last moment the resolution of
 * which DTD to use (unless of course we're assigned one).
 *
 * @update	gess5/18/98
 * @param 
 * @return  error code -- 0 if ok, non-zero if error.
 */
nsresult nsParser::WillBuildModel(nsString& aFilename,nsIDTD* aDefaultDTD){

  nsresult result=NS_OK;

  if(mParserContext){
    if(eOnStart==mParserContext->mStreamListenerState) {  
      mMajorIteration=-1; 
      mMinorIteration=-1; 
      if(eUnknownDetect==mParserContext->mAutoDetectStatus) {
        mParserContext->mDTD=aDefaultDTD;
        if(PR_TRUE==FindSuitableDTD(*mParserContext,mCommand,mParserContext->mScanner->GetBuffer())) {
          mParserContext->mParseMode=DetermineParseMode(*this);  
          mParserContext->mStreamListenerState=eOnDataAvail;
          mParserContext->mDTD->WillBuildModel( aFilename,
                                                PRBool(0==mParserContext->mPrevContext),
                                                mParserContext->mSourceType,
                                                mParserContext->mParseMode,
                                                mCommand,
                                                mSink);
        }//if        
      }//if
    }//if
  } 
  else result=kInvalidParserContext;    
  return result;
}

/**
 * This gets called when the parser is done with its input.
 * Note that the parser may have been called recursively, so we
 * have to check for a prev. context before closing out the DTD/sink.
 * @update	gess5/18/98
 * @param 
 * @return  error code -- 0 if ok, non-zero if error.
 */
nsresult nsParser::DidBuildModel(nsresult anErrorCode) {
  //One last thing...close any open containers.
  nsresult result=anErrorCode;

  if(mParserContext->mParserEnabled) {
    if((!mParserContext->mPrevContext) && (mParserContext->mDTD)) {
      result=mParserContext->mDTD->DidBuildModel(anErrorCode,PRBool(0==mParserContext->mPrevContext),this,mSink);
    }
  }//if

  return result;
}


/**
 * This method adds a new parser context to the list,
 * pushing the current one to the next position.
 * @update	gess7/22/98
 * @param   ptr to new context
 * @return  nada
 */
void nsParser::PushContext(CParserContext& aContext) {
  aContext.mPrevContext=mParserContext;  
  if (mParserContext) { 
     aContext.mParserEnabled = mParserContext->mParserEnabled; 
  } 
  mParserContext=&aContext;
}

/**
 * This method pops the topmost context off the stack,
 * returning it to the user. The next context  (if any)
 * becomes the current context.
 * @update	gess7/22/98
 * @return  prev. context
 */
CParserContext* nsParser::PopContext() {
  CParserContext* oldContext=mParserContext;
  if(oldContext) {
    mParserContext=oldContext->mPrevContext;
    // If the old context was blocked, propogate the blocked state
    // back to the newe one.
    if (mParserContext) {
      mParserContext->mParserEnabled = oldContext->mParserEnabled;
    }
  }
  return oldContext;
}

/**
 *  Call this when you want control whether or not the parser will parse
 *  and tokenize input (TRUE), or whether it just caches input to be 
 *  parsed later (FALSE).
 *  
 *  @update  gess 1/29/99
 *  @param   aState determines whether we parse/tokenize or just cache.
 *  @return  current state
 */
void nsParser::SetUnusedInput(nsString& aBuffer) {
  mUnusedInput=aBuffer;
}

/**
 *  Call this when you want to *force* the parser to terminate the
 *  parsing process altogether. This is binary -- so once you terminate
 *  you can't resume without restarting altogether.
 *  
 *  @update  gess 7/4/99
 *  @return  should return NS_OK once implemented
 */
nsresult nsParser::Terminate(void){
  nsresult result=NS_OK;
  if(mParserContext && mParserContext->mDTD)
    result=mParserContext->mDTD->Terminate();
  mInternalState=result;
  return result;
}

/**
 *  Call this when you want control whether or not the parser will parse
 *  and tokenize input (TRUE), or whether it just caches input to be 
 *  parsed later (FALSE).
 *  
 *  @update  gess 1/29/99
 *  @param   aState determines whether we parse/tokenize or just cache.
 *  @return  current state
 */
nsresult nsParser::EnableParser(PRBool aState){
  nsIParser* me = nsnull;

  // If the stream has already finished, there's a good chance
  // that we might start closing things down when the parser
  // is reenabled. To make sure that we're not deleted across
  // the reenabling process, hold a reference to ourselves.
  if (eOnStop == mParserContext->mStreamListenerState) {
    me = this;
    NS_ADDREF(me);
  }    

  // If we're reenabling the parser
  mParserContext->mParserEnabled=aState;
  nsresult result=NS_OK;
  if(aState) {
    result=ResumeParse();
    if(result!=NS_OK) 
      result=mInternalState;
  }
  else {
    MOZ_TIMER_DEBUGLOG(("Stop: Parse Time: nsParser::EnableParser(), this=%p\n", this));
    MOZ_TIMER_STOP(mParseTime);
  }  

  // Release reference if we added one at the top of this routine
  NS_IF_RELEASE(me);
  return result;
}

/**
 * Call this to query whether the parser is enabled or not.
 *
 *  @update  vidur 4/12/99
 *  @return  current state
 */
PRBool nsParser::IsParserEnabled() {
  return mParserContext->mParserEnabled;
}


/**
 *  This is the main controlling routine in the parsing process. 
 *  Note that it may get called multiple times for the same scanner, 
 *  since this is a pushed based system, and all the tokens may 
 *  not have been consumed by the scanner during a given invocation 
 *  of this method. 
 *
 *  @update  gess 01/04/99
 *  @param   aFilename -- const char* containing file to be parsed.
 *  @return  error code -- 0 if ok, non-zero if error.
 */
nsresult nsParser::Parse(nsIURI* aURL,nsIStreamObserver* aListener,PRBool aVerifyEnabled, void* aKey,eParseMode aMode) {  

  NS_PRECONDITION(0!=aURL,kNullURL);

  nsresult result=kBadURL;
  mDTDVerification=aVerifyEnabled;
  if(aURL) {
    char* spec;
    nsresult rv = aURL->GetSpec(&spec);
    if (rv != NS_OK) {      
      return rv;
    }
    nsAutoString theName(spec);
    nsCRT::free(spec);

    nsScanner* theScanner=new nsScanner(theName,PR_FALSE,mCharset,mCharsetSource);
    CParserContext* pc=new CParserContext(theScanner,aKey,aListener);
    if(pc && theScanner) {
      pc->mMultipart=PR_TRUE;
      pc->mContextType=CParserContext::eCTURL;
      PushContext(*pc);
      result=NS_OK;
    }
    else{
      result=mInternalState=NS_ERROR_HTMLPARSER_BADCONTEXT;
    }
  }  
  return result;
}


/**
 * Cause parser to parse input from given stream 
 * @update	vidur 12/11/98
 * @param   aStream is the i/o source
 * @return  error code -- 0 if ok, non-zero if error.
 */
nsresult nsParser::Parse(nsIInputStream& aStream,PRBool aVerifyEnabled, void* aKey,eParseMode aMode){

  mDTDVerification=aVerifyEnabled;
  nsresult  result=NS_ERROR_OUT_OF_MEMORY;

  //ok, time to create our tokenizer and begin the process
  nsAutoString theUnknownFilename("unknown");

  nsInputStream input(&aStream);
    
  nsScanner* theScanner=new nsScanner(theUnknownFilename,input,mCharset,mCharsetSource);
  CParserContext* pc=new CParserContext(theScanner,aKey,0);
  if(pc && theScanner) {
    PushContext(*pc);
    pc->mSourceType=kHTMLTextContentType;
    pc->mStreamListenerState=eOnStart;  
    pc->mMultipart=PR_FALSE;
    pc->mContextType=CParserContext::eCTStream;
    mParserContext->mScanner->Eof();
    result=ResumeParse();
    pc=PopContext();
    delete pc;
  }
  else{
    result=mInternalState=NS_ERROR_HTMLPARSER_BADCONTEXT;
  }  
  return result;
}


/**
 * Call this method if all you want to do is parse 1 string full of HTML text.
 * In particular, this method should be called by the DOM when it has an HTML
 * string to feed to the parser in real-time.
 *
 * @update	gess5/11/98
 * @param   aSourceBuffer contains a string-full of real content
 * @param   aContentType tells us what type of content to expect in the given string
 * @return  error code -- 0 if ok, non-zero if error.
 */
nsresult nsParser::Parse(const nsString& aSourceBuffer,void* aKey,const nsString& aContentType,PRBool aVerifyEnabled,PRBool aLastCall,eParseMode aMode){
 
  //NOTE: Make sure that updates to this method don't cause 
  //      bug #2361 to break again!  


#if 0
    //this is only for debug purposes
  aSourceBuffer.DebugDump();
#endif 

  nsresult result=NS_OK;
  nsParser* me = this;
  // Maintain a reference to ourselves so we don't go away
  // till we're completely done.
  NS_ADDREF(me);
  if(aSourceBuffer.Length() || mUnusedInput.Length()) {
    mDTDVerification=aVerifyEnabled;
    CParserContext* pc=0; 

    if((!mParserContext) || (mParserContext->mKey!=aKey))  {
      //only make a new context if we dont have one, OR if we do, but has a different context key...
    
      nsScanner* theScanner=new nsScanner(mUnusedInput,mCharset,mCharsetSource);
      pc=new CParserContext(theScanner,aKey, 0);
      if(pc && theScanner) {
        PushContext(*pc);
        pc->mStreamListenerState=eOnStart;  
        pc->mContextType=CParserContext::eCTString;
        pc->mSourceType=aContentType; 
        mUnusedInput.Truncate(0);
      } 
      else {
        NS_RELEASE(me);        
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }
    else {
      pc=mParserContext;
      pc->mScanner->Append(mUnusedInput);
    }

    pc->mScanner->Append(aSourceBuffer);

    if (nsnull != pc->mPrevContext) {
      pc->mMultipart = (pc->mPrevContext->mMultipart || !aLastCall);
    }
    else {
      pc->mMultipart=!aLastCall;
    }
    result=ResumeParse();
    if(aLastCall) {
      pc->mScanner->CopyUnusedData(mUnusedInput);
      pc=PopContext(); 
      delete pc;
    }//if
  }//if
  NS_RELEASE(me);  
  return result;
}

/**
 *  Call this method to test whether a given fragment is valid within a given context-stack.
 *  @update  gess 04/01/99
 *  @param   aSourceBuffer contains the content blob you're trying to insert
 *  @param   aInsertPos tells us where in the context stack you're trying to do the insertion
 *  @param   aContentType tells us what kind of stuff you're inserting
 *  @return  TRUE if valid, otherwise FALSE
 */
PRBool nsParser::IsValidFragment(const nsString& aSourceBuffer,nsITagStack& aStack,PRUint32 anInsertPos,const nsString& aContentType,eParseMode aMode){

  /************************************************************************************
    This method works like this:
      1. Convert aStack to a markup string
      2. Append a "sentinel" tag to markup string so we know where new content is inserted
      3. Append new context to markup stack
      4. Call the normal parse() methods for a string, using an HTMLContentSink.
         The output of this call is stored in an outputstring
      5. Scan the output string looking for markup inside our sentinel. If non-empty
         then we have to assume that the fragment is valid (at least in part)
   ************************************************************************************/

  nsAutoString  theContext;
  PRUint32 theCount=aStack.GetSize();
  PRUint32 theIndex=0;
  while(theIndex++<theCount){
    theContext.Append("<");
    theContext.Append(aStack.TagAt(theCount-theIndex));
    theContext.Append(">");
  }
  theContext.Append("<endnote>");       //XXXHack! I'll make this better later.
  nsAutoString theBuffer(theContext);
  theBuffer.Append(aSourceBuffer);
  
  PRBool result=PR_FALSE;
  if(theBuffer.Length()){
    //now it's time to try to build the model from this fragment

    nsString theOutput("");
    nsIHTMLContentSink*  theSink=0;
    nsresult theResult=NS_New_HTML_ContentSinkStream(&theSink,&theOutput,0);
    SetContentSink(theSink);
    theResult=Parse(theBuffer,(void*)&theBuffer,aContentType,PR_FALSE,PR_TRUE);
    theOutput.StripWhitespace();
    if(NS_OK==theResult){
      theOutput.Cut(0,theContext.Length());
      PRInt32 aPos=theOutput.RFind("</endnote>");
      if(-1<aPos)
        theOutput.Truncate(aPos);
      result=PRBool(0<theOutput.Length());
    }
  }
  return result;
}


/**
 *
 *  @update  gess 04/01/99
 *  @param   
 *  @return  
 */
nsresult nsParser::ParseFragment(const nsString& aSourceBuffer,void* aKey,nsITagStack& aStack,PRUint32 anInsertPos,const nsString& aContentType,eParseMode aMode){

  nsresult result=NS_OK;
  nsAutoString  theContext;
  PRUint32 theCount=aStack.GetSize();
  PRUint32 theIndex=0;
  while(theIndex++<theCount){
    theContext.Append("<");
    theContext.Append(aStack.TagAt(theCount-theIndex));
    theContext.Append(">");
  }
  theContext.Append("<endnote>");       //XXXHack! I'll make this better later.
  nsAutoString theBuffer(theContext);

#if 0
      //use this to force a buffer-full of content as part of a paste operation...
    theBuffer.Append("<title>title</title><a href=\"one\">link</a>");
#else
      //this is the normal code path for paste...
    theBuffer.Append(aSourceBuffer);  
#endif

  if(theBuffer.Length()){
    //now it's time to try to build the model from this fragment

    mObserversEnabled=PR_FALSE; //disable observers for fragments
    result=Parse(theBuffer,(void*)&theBuffer,aContentType,PR_FALSE,PR_TRUE);
    mObserversEnabled=PR_TRUE; //now reenable.
  }

  return result;
}

 
/**
 *  This routine is called to cause the parser to continue
 *  parsing it's underlying stream. This call allows the
 *  parse process to happen in chunks, such as when the
 *  content is push based, and we need to parse in pieces.
 *  
 *  @update  gess 01/04/99 
 *  @param   
 *  @return  error code -- 0 if ok, non-zero if error.
 */
nsresult nsParser::ResumeParse(nsIDTD* aDefaultDTD, PRBool aIsFinalChunk) {
  nsresult result=NS_OK;
  if(mParserContext->mParserEnabled && mInternalState!=NS_ERROR_HTMLPARSER_STOPPARSING) {

    MOZ_TIMER_DEBUGLOG(("Start: Parse Time: nsParser::ResumeParse(), this=%p\n", this));
    MOZ_TIMER_START(mParseTime);

    result=WillBuildModel(mParserContext->mScanner->GetFilename(),aDefaultDTD);
    if(mParserContext->mDTD) {
      mParserContext->mDTD->WillResumeParse();
      if(NS_OK==result) {     
        nsresult   theTokenizerResult=NS_OK;
       
        while(result==NS_OK) {

          if(mUnusedInput.Length()>0) {
            if(mParserContext->mScanner) {
              // -- Ref: Bug# 22485 --
              // Insert the unused input into the source buffer 
              // as if it was read from the input stream. 
              // Adding Insert() per vidur!!
              mParserContext->mScanner->Insert(mUnusedInput);
              mUnusedInput.Truncate(0);
            }
          }

          result=Tokenize(aIsFinalChunk);

          if(result!=NS_OK) theTokenizerResult=result; 

          result=BuildModel();
        
          if(result==NS_ERROR_HTMLPARSER_STOPPARSING) mInternalState=result;

          // Make sure not to stop parsing too early. Therefore, before shutting down the 
          // parser, it's important to check whether the input buffer has been scanned to 
          // completion ( theTokenizerResult should be kEOF ). kEOF -> End of buffer.
          if((!mParserContext->mMultipart) || (mInternalState==NS_ERROR_HTMLPARSER_STOPPARSING) || 
            ((eOnStop==mParserContext->mStreamListenerState) && (NS_OK==result) && (theTokenizerResult==kEOF))){

            DidBuildModel(mStreamStatus);          

            MOZ_TIMER_DEBUGLOG(("Stop: Parse Time: nsParser::ResumeParse(), this=%p\n", this));
            MOZ_TIMER_STOP(mParseTime);

            MOZ_TIMER_LOG(("Parse Time (this=%p): ", this));
            MOZ_TIMER_PRINT(mParseTime);

            MOZ_TIMER_LOG(("DTD Time: "));
            MOZ_TIMER_PRINT(mDTDTime);

            MOZ_TIMER_LOG(("Tokenize Time: "));
            MOZ_TIMER_PRINT(mTokenizeTime);
              
            return mInternalState;
          }
          // If we're told to block the parser, we disable
          // all further parsing (and cache any data coming
          // in) until the parser is enabled.
          //PRUint32 b1=NS_ERROR_HTMLPARSER_BLOCK;
          else if(NS_ERROR_HTMLPARSER_BLOCK==result) {
             mParserContext->mDTD->WillInterruptParse();
             result=EnableParser(PR_FALSE);
             break;
          }
          // If we're at the end of the current scanner buffer,
          // we interrupt parsing and wait for the next one
          else if (theTokenizerResult==kEOF) { 
            mParserContext->mDTD->WillInterruptParse();
            break;
          }
        }//while
      }//if
    }//if
    else {
      mInternalState=result=NS_ERROR_HTMLPARSER_UNRESOLVEDDTD;
    }
  }//if

  MOZ_TIMER_DEBUGLOG(("Stop: Parse Time: nsParser::ResumeParse(), this=%p\n", this));
  MOZ_TIMER_STOP(mParseTime);

  return result;
}

/**
 *  This is where we loop over the tokens created in the 
 *  tokenization phase, and try to make sense out of them. 
 *
 *  @update  gess 01/04/99
 *  @param   
 *  @return  error code -- 0 if ok, non-zero if error.
 */
nsresult nsParser::BuildModel() {
  
  //nsDequeIterator e=mParserContext->mTokenDeque.End(); 

//  if(!mParserContext->mCurrentPos)
//    mParserContext->mCurrentPos=new nsDequeIterator(mParserContext->mTokenDeque.Begin());

    //Get the root DTD for use in model building...

  CParserContext* theRootContext=mParserContext;
  nsITokenizer*   theTokenizer=0;

  nsresult result=mParserContext->mDTD->GetTokenizer(theTokenizer);
  if(theTokenizer){

    while(theRootContext->mPrevContext) {
      theRootContext=theRootContext->mPrevContext;
    }

    nsIDTD* theRootDTD=theRootContext->mDTD;
    if(theRootDTD) {      
      MOZ_TIMER_START(mDTDTime);
      result=theRootDTD->BuildModel(this,theTokenizer,mTokenObserver,mSink);      
      MOZ_TIMER_STOP(mDTDTime);
    }
  }
  else{
    mInternalState=result=NS_ERROR_HTMLPARSER_BADTOKENIZER;
  }
  return result;
}


/**
 * 
 * @update	gess1/22/99
 * @param 
 * @return
 */
nsITokenizer* nsParser::GetTokenizer(void) {
  nsITokenizer* theTokenizer=0;
  if(mParserContext && mParserContext->mDTD) {
    mParserContext->mDTD->GetTokenizer(theTokenizer);
  }
  return theTokenizer;
}

/*******************************************************************
  These methods are used to talk to the netlib system...
 *******************************************************************/

/**
 *  
 *  
 *  @update  gess 5/12/98
 *  @param   
 *  @return  error code -- 0 if ok, non-zero if error.
 */
nsresult
nsParser::OnProgress(nsIChannel* channel, nsISupports* aContext, PRUint32 aProgress, PRUint32 aProgressMax)
{
  nsresult result=0;
  if (nsnull != mProgressEventSink) {
    mProgressEventSink->OnProgress(channel, aContext, aProgress, aProgressMax);
  }
  return result;
}

/**
 *  
 *  
 *  @update  gess 5/12/98
 *  @param   
 *  @return  error code -- 0 if ok, non-zero if error.
 */
nsresult
nsParser::OnStatus(nsIChannel* channel, nsISupports* aContext, const PRUnichar* aMsg)
{
  nsresult result=0;
  if (nsnull != mProgressEventSink) {
    mProgressEventSink->OnStatus(channel, aContext, aMsg);
  }
  return result;
}

#ifdef rickgdebug
#include <fstream.h>
  fstream* gOutFile;
#endif

/**
 *  
 *  
 *  @update  gess 5/12/98
 *  @param   
 *  @return  error code -- 0 if ok, non-zero if error.
 */
nsresult nsParser::OnStartRequest(nsIChannel* channel, nsISupports* aContext)
{
  NS_PRECONDITION((eNone==mParserContext->mStreamListenerState),kBadListenerInit);

  if (nsnull != mObserver) {
    mObserver->OnStartRequest(channel, aContext);
  }
  mParserContext->mStreamListenerState=eOnStart;
  mParserContext->mAutoDetectStatus=eUnknownDetect;
  mParserContext->mDTD=0;
  nsresult rv;
  char* contentType = nsnull;
  rv = channel->GetContentType(&contentType);
  if (NS_SUCCEEDED(rv))
  {
    mParserContext->mSourceType = contentType;
	nsCRT::free(contentType);
  }
  else
    NS_ASSERTION(contentType, "parser needs a content type to find a dtd");

#ifdef rickgdebug
  gOutFile= new fstream("c:/temp/out.file",ios::trunc);
#endif

  return NS_OK;
}


#define UCS2_BE "UTF-16BE"
#define UCS2_LE "UTF-16LE"
#define UCS4_BE "UTF-32BE"
#define UCS4_LE "UTF-32LE"
#define UCS4_2143 "X-ISO-10646-UCS-4-2143"
#define UCS4_3412 "X-ISO-10646-UCS-4-3412"

static PRBool detectByteOrderMark(const unsigned char* aBytes, PRInt32 aLen, nsString& oCharset, nsCharsetSource& oCharsetSource) {
 oCharsetSource= kCharsetFromAutoDetection;
 oCharset = "";
 // see http://www.w3.org/TR/1998/REC-xml-19980210#sec-oCharseting
 // for details
 switch(aBytes[0])
	 {
   case 0x00:
     if(0x00==aBytes[1]) {
        // 00 00
        if((0x00==aBytes[2]) && (0x3C==aBytes[3])) {
           // 00 00 00 3C UCS-4, big-endian machine (1234 order)
           oCharset = UCS4_BE;
        } else if((0x3C==aBytes[2]) && (0x00==aBytes[3])) {
           // 00 00 3C 00 UCS-4, unusual octet order (2143)
           oCharset = UCS4_2143;
        } 
     } else if(0x3C==aBytes[1]) {
        // 00 3C
        if((0x00==aBytes[2]) && (0x00==aBytes[3])) {
           // 00 3C 00 00 UCS-4, unusual octet order (3412)
           oCharset = UCS4_3412;
        } else if((0x3C==aBytes[2]) && (0x3F==aBytes[3])) {
           // 00 3C 00 3F UTF-16, big-endian, no Byte Order Mark
           oCharset = UCS2_BE; // should change to UTF-16BE
        } 
     }
   break;
   case 0x3C:
     if(0x00==aBytes[1]) {
        // 3C 00
        if((0x00==aBytes[2]) && (0x00==aBytes[3])) {
           // 3C 00 00 00 UCS-4, little-endian machine (4321 order)
           oCharset = UCS4_LE;
        } else if((0x3F==aBytes[2]) && (0x00==aBytes[3])) {
           // 3C 00 3F 00 UTF-16, little-endian, no Byte Order Mark
           oCharset = UCS2_LE; // should change to UTF-16LE
        } 
     } else if((0x3C==aBytes[0]) && (0x3F==aBytes[1]) &&
               (0x78==aBytes[2]) && (0x6D==aBytes[3]) &&
               (0 == PL_strncmp("<?xml version", (char*)aBytes, 13 ))) {
       // 3C 3F 78 6D
       nsAutoString firstXbytes("");
       firstXbytes.Append((const char*)aBytes, (PRInt32)
                       ((aLen > XMLENCODING_PEEKBYTES)?
                       XMLENCODING_PEEKBYTES:
                       aLen)); 
       PRInt32 xmlDeclEnd = firstXbytes.Find("?>", PR_FALSE, 13);
	   // 27 == strlen("<xml? version="1" encoding=");
       if((kNotFound != xmlDeclEnd) &&(xmlDeclEnd > 27 )){
           firstXbytes.Cut(xmlDeclEnd, firstXbytes.Length()-xmlDeclEnd);
           PRInt32 encStart = firstXbytes.Find("encoding", PR_FALSE,13);
           if(kNotFound != encStart) {
             encStart = firstXbytes.FindCharInSet("\"'", encStart+8);
                              // 8 == strlen("encoding")
             if(kNotFound != encStart) {
                PRUnichar q = firstXbytes.CharAt(encStart); 
                PRInt32 encEnd = firstXbytes.FindChar(q, PR_FALSE, encStart+1);
                if(kNotFound != encEnd) {
                   PRInt32 count = encEnd - encStart -1;
                   if(count >0) {
                      firstXbytes.Mid(oCharset,(encStart+1), count);
                      oCharsetSource= kCharsetFromMetaTag;
                   }
                }
             }
           }
       }
     }
   break;
   case 0xFE:
     if(0xFF==aBytes[1]) {
        // FE FF
        // UTF-16, big-endian 
        oCharset = UCS2_BE; // should change to UTF-16BE
        oCharsetSource= kCharsetFromByteOrderMark;
     }
   break;
   case 0xFF:
     if(0xFE==aBytes[1]) {
        // FF FE
        // UTF-16, little-endian 
        oCharset = UCS2_LE; // should change to UTF-16LE
        oCharsetSource= kCharsetFromByteOrderMark;
     }
   break;
   // case 0x4C: if((0x6F==aBytes[1]) && ((0xA7==aBytes[2] && (0x94==aBytes[3])) {
   //   We do not care EBCIDIC here....
   // }
   // break;
 }  // switch
 return oCharset.Length() > 0;
}


/**
 *  
 *  
 *  @update  gess 1/4/99
 *  @param   pIStream contains the input chars
 *  @param   length is the number of bytes waiting input
 *  @return  error code (usually 0)
 */
nsresult nsParser::OnDataAvailable(nsIChannel* channel, nsISupports* aContext,
                                   nsIInputStream *pIStream, PRUint32 sourceOffset, PRUint32 aLength)
{

  NS_PRECONDITION(((eOnStart==mParserContext->mStreamListenerState)||(eOnDataAvail==mParserContext->mStreamListenerState)),kOnStartNotCalled);

  if(eInvalidDetect==mParserContext->mAutoDetectStatus) {
    if(mParserContext->mScanner) {
      mParserContext->mScanner->GetBuffer().Truncate();
    }
  }

  PRInt32 newLength=(aLength>mParserContext->mTransferBufferSize) ? aLength : mParserContext->mTransferBufferSize;
  if(!mParserContext->mTransferBuffer) {
    mParserContext->mTransferBufferSize=newLength;
    mParserContext->mTransferBuffer=new char[newLength+20];
  }
  else if(aLength>mParserContext->mTransferBufferSize){
    delete [] mParserContext->mTransferBuffer;
    mParserContext->mTransferBufferSize=newLength;
    mParserContext->mTransferBuffer=new char[newLength+20];
  }

  nsresult result=NS_OK;

  if(mParserContext->mTransferBuffer) {

      //We need to add code to defensively deal with the case where the transfer buffer is null.

    PRUint32  theTotalRead=0; 
    PRUint32  theNumRead=1;   //init to a non-zero value
    int       theStartPos=0;   

    PRBool needCheckFirst4Bytes = 
            ((0 == sourceOffset) && (mCharsetSource<kCharsetFromAutoDetection));
    while ((theNumRead>0) && (aLength>theTotalRead) && (NS_OK==result)) {
      result = pIStream->Read(mParserContext->mTransferBuffer, aLength, &theNumRead);
      if(NS_SUCCEEDED(result) && (theNumRead>0)) {
        if(needCheckFirst4Bytes && (theNumRead >= 4)) {
           nsCharsetSource guessSource;
           nsAutoString guess("");
         
           needCheckFirst4Bytes = PR_FALSE;
           if(detectByteOrderMark((const unsigned char*)mParserContext->mTransferBuffer,
                                  theNumRead, guess, guessSource)) 
           {
  #ifdef DEBUG_XMLENCODING
              printf("xmlencoding detect- %s\n", guess.ToNewCString());
  #endif
              this->SetDocumentCharset(guess, guessSource);
			        mParserContext->mScanner->SetDocumentCharset(guess, guessSource);
           }
        }
        theTotalRead+=theNumRead;
        if(mParserFilter)
           mParserFilter->RawBuffer(mParserContext->mTransferBuffer, &theNumRead);

  #ifdef NS_DEBUG
        unsigned int index=0;
        for(index=0;index<theNumRead;index++) {
          if(0==mParserContext->mTransferBuffer[index]){
            printf("\nNull found at buffer[%i] provided by netlib...\n",index);
            break;
          }
        }
  #endif

        mParserContext->mScanner->Append(mParserContext->mTransferBuffer,theNumRead);

  #ifdef rickgdebug
        mParserContext->mTransferBuffer[theNumRead]=0;
        mParserContext->mTransferBuffer[theNumRead+1]=0;
        mParserContext->mTransferBuffer[theNumRead+2]=0;
        (*gOutFile) << mParserContext->mTransferBuffer;
  #endif

      } //if
      theStartPos+=theNumRead;
    }//while

    result=ResumeParse();     
  }
  return result;
}

/**
 *  This is called by the networking library once the last block of data
 *  has been collected from the net.
 *  
 *  @update  gess 04/01/99
 *  @param   
 *  @return  
 */
nsresult nsParser::OnStopRequest(nsIChannel* channel, nsISupports* aContext,
                                 nsresult status, const PRUnichar* aMsg)
{  

  nsresult result=NS_OK;
  
  if(eOnStart==mParserContext->mStreamListenerState) {
    //If you're here, then OnDataAvailable() never got called. 
    //Prior to necko, we never dealt with this case, but the problem may have existed.
    //What we'll do (for now at least) is construct the worlds smallest HTML document.
    nsAutoString  temp("<BODY></BODY>");
    mParserContext->mScanner->Append(temp);
    result=ResumeParse(nsnull, PR_TRUE);    
  }

  mParserContext->mStreamListenerState=eOnStop;
  mStreamStatus=status;

  if(mParserFilter)
     mParserFilter->Finish();

  mParserContext->mScanner->SetIncremental(PR_FALSE);
  result=ResumeParse(nsnull, PR_TRUE);
  
  // If the parser isn't enabled, we don't finish parsing till
  // it is reenabled.


  // XXX Should we wait to notify our observers as well if the
  // parser isn't yet enabled?
  if (nsnull != mObserver) {
    mObserver->OnStopRequest(channel, aContext, status, aMsg);
  }

#ifdef rickgdebug
  if(gOutFile){
    gOutFile->close();
    delete gOutFile;
  }
#endif

  return result;
}


/*******************************************************************
  Here comes the tokenization methods...
 *******************************************************************/


/**
 *  Part of the code sandwich, this gets called right before
 *  the tokenization process begins. The main reason for
 *  this call is to allow the delegate to do initialization.
 *  
 *  @update  gess 01/04/99
 *  @param   
 *  @return  TRUE if it's ok to proceed
 */
PRBool nsParser::WillTokenize(PRBool aIsFinalChunk){
  nsITokenizer* theTokenizer=0;
  nsresult result=mParserContext->mDTD->GetTokenizer(theTokenizer);
  if (theTokenizer) {
    result = theTokenizer->WillTokenize(aIsFinalChunk);
  }  
  return result;
}


/**
 *  This is the primary control routine to consume tokens. 
 *	It iteratively consumes tokens until an error occurs or 
 *	you run out of data.
 *  
 *  @update  gess 01/04/99
 *  @return  error code -- 0 if ok, non-zero if error.
 */
nsresult nsParser::Tokenize(PRBool aIsFinalChunk){

  ++mMajorIteration; 

  nsITokenizer* theTokenizer=0;
  nsresult result=mParserContext->mDTD->GetTokenizer(theTokenizer);

  if(theTokenizer){    
    PRBool flushTokens=PR_FALSE;

    MOZ_TIMER_START(mTokenizeTime);

    WillTokenize(aIsFinalChunk);
    while(NS_SUCCEEDED(result)) {
      mParserContext->mScanner->Mark();
      ++mMinorIteration;
      result=theTokenizer->ConsumeToken(*mParserContext->mScanner,flushTokens);
      if(NS_FAILED(result)) {
        mParserContext->mScanner->RewindToMark();
        if(kEOF==result){
          break;
        }
        else if(NS_ERROR_HTMLPARSER_STOPPARSING==result)
          return Terminate();
      }
      else if(flushTokens) {
        // Flush tokens on seeing </SCRIPT> -- Ref: Bug# 22485 --
        // Also remember to update the marked position.
        mParserContext->mScanner->Mark();
        break; 
      }
    } 
    DidTokenize(aIsFinalChunk);

    MOZ_TIMER_STOP(mTokenizeTime);
  }  
  else{
    result=mInternalState=NS_ERROR_HTMLPARSER_BADTOKENIZER;
  }
  return result;
}

/**
 *  This is the tail-end of the code sandwich for the
 *  tokenization process. It gets called once tokenziation
 *  has completed for each phase.
 *  
 *  @update  gess 01/04/99
 *  @param   
 *  @return  TRUE if all went well
 */
PRBool nsParser::DidTokenize(PRBool aIsFinalChunk){
  PRBool result=PR_TRUE;

  nsITokenizer* theTokenizer=0;
  nsresult rv=mParserContext->mDTD->GetTokenizer(theTokenizer);

  if (NS_SUCCEEDED(rv) && theTokenizer) {
    result = theTokenizer->DidTokenize(aIsFinalChunk);
    if(mTokenObserver) {
      PRInt32 theCount=theTokenizer->GetCount();
      PRInt32 theIndex;
      for(theIndex=0;theIndex<theCount;theIndex++){
        if((*mTokenObserver)(theTokenizer->GetTokenAt(theIndex))){
          //add code here to pull unwanted tokens out of the stack...
        }
      }//for      
    }//if
  }
  return result;
}

void nsParser::DebugDumpSource(nsOutputStream& aStream) {
  PRInt32 theIndex=-1;

  nsITokenizer* theTokenizer=0;
  if(NS_SUCCEEDED(mParserContext->mDTD->GetTokenizer(theTokenizer))){
    CToken* theToken;
    while(nsnull != (theToken=theTokenizer->GetTokenAt(++theIndex))) {
      // theToken->DebugDumpToken(out);
      theToken->DebugDumpSource(aStream);
    }
  }
}


/**
 * Call this to get a newly constructed tagstack
 * @update	gess 5/05/99
 * @param   aTagStack is an out parm that will contain your result
 * @return  NS_OK if successful, or NS_HTMLPARSER_MEMORY_ERROR on error
 */
nsresult nsParser::CreateTagStack(nsITagStack** aTagStack){
  *aTagStack=new nsTagStack();
  if(*aTagStack)
    return NS_OK;
  return NS_ERROR_OUT_OF_MEMORY;
}

/** 
 * Get the DTD associated with this parser
 * @update vidur 9/29/99
 * @param aDTD out param that will contain the result
 * @return NS_OK if successful, NS_ERROR_FAILURE for runtime error
 */
NS_IMETHODIMP 
nsParser::GetDTD(nsIDTD** aDTD)
{
  if (mParserContext) {
    *aDTD = mParserContext->mDTD;
    NS_IF_ADDREF(mParserContext->mDTD);
  }
  
  return NS_OK;
}


/** 
 * Get the observer service
 *
 * @update rickg 11/22/99
 * @return ptr to server or NULL
 */
CObserverService* nsParser::GetObserverService(void) { 
    //XXX Hack! this should be XPCOM based!
  if(mObserversEnabled)
    return &mObserverService; 
  return 0;
}

