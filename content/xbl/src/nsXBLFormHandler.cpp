/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Original Author: David W. Hyatt (hyatt@netscape.com)
 *
 */

#include "nsCOMPtr.h"
#include "nsIXBLPrototypeHandler.h"
#include "nsXBLFormHandler.h"
#include "nsIContent.h"
#include "nsIAtom.h"
#include "nsINameSpaceManager.h"
#include "nsIScriptContext.h"
#include "nsIScriptGlobalObject.h"
#include "nsIDocument.h"
#include "nsIDOMDocument.h"
#include "nsIEventListenerManager.h"
#include "nsIDOMEventReceiver.h"
#include "nsXBLBinding.h"
#include "nsIPrivateDOMEvent.h"
#include "nsIDOMWindowInternal.h"
#include "nsIPref.h"
#include "nsIServiceManager.h"
#include "nsIURI.h"
#include "nsXPIDLString.h"

PRUint32 nsXBLFormHandler::gRefCnt = 0;
nsIAtom* nsXBLFormHandler::kSubmitAtom = nsnull;
nsIAtom* nsXBLFormHandler::kResetAtom = nsnull;
nsIAtom* nsXBLFormHandler::kInputAtom = nsnull;
nsIAtom* nsXBLFormHandler::kSelectAtom = nsnull;
nsIAtom* nsXBLFormHandler::kChangeAtom = nsnull;

nsXBLFormHandler::nsXBLFormHandler(nsIDOMEventReceiver* aReceiver, nsIXBLPrototypeHandler* aHandler)
:nsXBLEventHandler(aReceiver,aHandler)
{
  gRefCnt++;
  if (gRefCnt == 1) {
    kInputAtom = NS_NewAtom("input");
    kSelectAtom = NS_NewAtom("select");
    kChangeAtom = NS_NewAtom("change");
    kSubmitAtom = NS_NewAtom("submit");
    kResetAtom = NS_NewAtom("reset");
  }
}

nsXBLFormHandler::~nsXBLFormHandler()
{
  gRefCnt--;
  if (gRefCnt == 0) {
    NS_RELEASE(kInputAtom);
    NS_RELEASE(kSubmitAtom);
    NS_RELEASE(kResetAtom);
    NS_RELEASE(kSelectAtom);
    NS_RELEASE(kChangeAtom);
  }
}

NS_IMPL_ISUPPORTS_INHERITED1(nsXBLFormHandler, nsXBLEventHandler, nsIDOMFormListener)

nsresult nsXBLFormHandler::Submit(nsIDOMEvent* aEvent)
{
  if (!mProtoHandler)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAtom> eventName;
  mProtoHandler->GetEventName(getter_AddRefs(eventName));

  if (eventName.get() != kSubmitAtom)
    return NS_OK;

  mProtoHandler->ExecuteHandler(mEventReceiver, aEvent);
  return NS_OK;
}

nsresult nsXBLFormHandler::Reset(nsIDOMEvent* aEvent)
{
  if (!mProtoHandler)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAtom> eventName;
  mProtoHandler->GetEventName(getter_AddRefs(eventName));

  if (eventName.get() != kResetAtom)
    return NS_OK;

  mProtoHandler->ExecuteHandler(mEventReceiver, aEvent);
  return NS_OK;
}

nsresult nsXBLFormHandler::Select(nsIDOMEvent* aEvent)
{
  if (!mProtoHandler)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAtom> eventName;
  mProtoHandler->GetEventName(getter_AddRefs(eventName));

  if (eventName.get() != kSelectAtom)
    return NS_OK;

  mProtoHandler->ExecuteHandler(mEventReceiver, aEvent);
  return NS_OK;
}

nsresult nsXBLFormHandler::Change(nsIDOMEvent* aEvent)
{
  if (!mProtoHandler)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAtom> eventName;
  mProtoHandler->GetEventName(getter_AddRefs(eventName));

  if (eventName.get() != kChangeAtom)
    return NS_OK;

  mProtoHandler->ExecuteHandler(mEventReceiver, aEvent);
  return NS_OK;
}

nsresult nsXBLFormHandler::Input(nsIDOMEvent* aEvent)
{
  if (!mProtoHandler)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAtom> eventName;
  mProtoHandler->GetEventName(getter_AddRefs(eventName));

  if (eventName.get() != kInputAtom)
    return NS_OK;

  mProtoHandler->ExecuteHandler(mEventReceiver, aEvent);
  return NS_OK;
}

///////////////////////////////////////////////////////////////////////////////////

nsresult
NS_NewXBLFormHandler(nsIDOMEventReceiver* aRec, nsIXBLPrototypeHandler* aHandler, 
                     nsXBLFormHandler** aResult)
{
  *aResult = new nsXBLFormHandler(aRec, aHandler);
  if (!*aResult)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*aResult);
  return NS_OK;
}
