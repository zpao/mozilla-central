/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "License"); you may not use this file except in
 * compliance with the License.  You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.  See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are Copyright (C) 1998
 * Netscape Communications Corporation.  All Rights Reserved.
 */

/* Do not edit - generated by gentags.pl */
#ifndef nsHTMLTags_h___
#define nsHTMLTags_h___
#include "nshtmlpars.h"
enum nsHTMLTag {
  /* this enum must be first and must be zero */
  eHTMLTag_unknown=0,

  /* begin tag enums */
  eHTMLTag_a=1, eHTMLTag_abbr=2, eHTMLTag_acronym=3, eHTMLTag_address=4, 
  eHTMLTag_applet=5, eHTMLTag_area=6, eHTMLTag_b=7, eHTMLTag_base=8, 
  eHTMLTag_basefont=9, eHTMLTag_bdo=10, eHTMLTag_big=11, eHTMLTag_blink=12, 
  eHTMLTag_blockquote=13, eHTMLTag_body=14, eHTMLTag_br=15, 
  eHTMLTag_button=16, eHTMLTag_caption=17, eHTMLTag_center=18, 
  eHTMLTag_cite=19, eHTMLTag_code=20, eHTMLTag_col=21, eHTMLTag_colgroup=22, 
  eHTMLTag_dd=23, eHTMLTag_del=24, eHTMLTag_dfn=25, eHTMLTag_dir=26, 
  eHTMLTag_div=27, eHTMLTag_dl=28, eHTMLTag_dt=29, eHTMLTag_em=30, 
  eHTMLTag_embed=31, eHTMLTag_fieldset=32, eHTMLTag_font=33, 
  eHTMLTag_form=34, eHTMLTag_frame=35, eHTMLTag_frameset=36, eHTMLTag_h1=37, 
  eHTMLTag_h2=38, eHTMLTag_h3=39, eHTMLTag_h4=40, eHTMLTag_h5=41, 
  eHTMLTag_h6=42, eHTMLTag_head=43, eHTMLTag_hr=44, eHTMLTag_html=45, 
  eHTMLTag_i=46, eHTMLTag_iframe=47, eHTMLTag_ilayer=48, eHTMLTag_img=49, 
  eHTMLTag_input=50, eHTMLTag_ins=51, eHTMLTag_isindex=52, eHTMLTag_kbd=53, 
  eHTMLTag_keygen=54, eHTMLTag_label=55, eHTMLTag_layer=56, 
  eHTMLTag_legend=57, eHTMLTag_li=58, eHTMLTag_link=59, eHTMLTag_listing=60, 
  eHTMLTag_map=61, eHTMLTag_menu=62, eHTMLTag_meta=63, eHTMLTag_multicol=64, 
  eHTMLTag_nobr=65, eHTMLTag_noembed=66, eHTMLTag_noframes=67, 
  eHTMLTag_nolayer=68, eHTMLTag_noscript=69, eHTMLTag_object=70, 
  eHTMLTag_ol=71, eHTMLTag_optgroup=72, eHTMLTag_option=73, eHTMLTag_p=74, 
  eHTMLTag_param=75, eHTMLTag_plaintext=76, eHTMLTag_pre=77, eHTMLTag_q=78, 
  eHTMLTag_s=79, eHTMLTag_samp=80, eHTMLTag_script=81, eHTMLTag_select=82, 
  eHTMLTag_server=83, eHTMLTag_small=84, eHTMLTag_spacer=85, 
  eHTMLTag_span=86, eHTMLTag_strike=87, eHTMLTag_strong=88, 
  eHTMLTag_style=89, eHTMLTag_sub=90, eHTMLTag_sup=91, eHTMLTag_table=92, 
  eHTMLTag_tbody=93, eHTMLTag_td=94, eHTMLTag_textarea=95, eHTMLTag_tfoot=96, 
  eHTMLTag_th=97, eHTMLTag_thead=98, eHTMLTag_title=99, eHTMLTag_tr=100, 
  eHTMLTag_tt=101, eHTMLTag_u=102, eHTMLTag_ul=103, eHTMLTag_var=104, 
  eHTMLTag_wbr=105, eHTMLTag_xmp=106, 

  /* The remaining enums are not for tags */
  eHTMLTag_text=107, eHTMLTag_whitespace=108, eHTMLTag_newline=109, 
  eHTMLTag_comment=110, eHTMLTag_entity=111, eHTMLTag_userdefined=112, 
  eHTMLTag_secret_h1style=113, eHTMLTag_secret_h2style=114, 
  eHTMLTag_secret_h3style=115, eHTMLTag_secret_h4style=116, 
  eHTMLTag_secret_h5style=117, eHTMLTag_secret_h6style=118
};
#define NS_HTML_TAG_MAX 106

extern NS_HTMLPARS nsHTMLTag NS_TagToEnum(const char* aTag);
extern NS_HTMLPARS const char* NS_EnumToTag(nsHTMLTag aEnum);

#endif /* nsHTMLTags_h___ */
