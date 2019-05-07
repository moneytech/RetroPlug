<?php



function process_file($infn, $outfn)
{

$in = fopen($infn,"r");
if (!$in) die("error opening input $infn\n");
$out = fopen($outfn,"w");
if (!$out) die("error opening output $outfn\n");

fputs($out,"// THIS FILE AUTOGENERATED FROM $infn by a2i.php\n\n");

$inblock=0;

fputs($out,
  "#if EEL_F_SIZE == 8\n" .
  "  #define EEL_ASM_TYPE qword ptr\n" .
  "#else\n" .
  "  #define EEL_ASM_TYPE dword ptr\n" .
  "#endif\n\n");
 
$labelcnt=0;

while (($line = fgets($in)))
{
  $line = rtrim($line);
  if (trim($line) == "FUNCTION_MARKER")
  {
    fputs($out,"_emit 0x89;\n");
    for ($tmp=0;$tmp<11;$tmp++) fputs($out,"_emit 0x90;\n");
    continue;
  }
  $nowrite=0;
  
  {
    if (!$inblock)
    {
      if (substr(trim($line),0,5)== "void ")
        $line = "__declspec(naked) " . trim($line);
      if (strstr($line,"__asm__("))
      {
        $line=str_replace("__asm__(", "__asm {", $line);
        $inblock=1;
        if (isset($bthist)) unset($bthist);
        if (isset($btfut)) unset($btfut);
        $bthist = array();
        $btfut = array();
      }
    }

    if ($inblock)
    {
      if (substr(trim($line),-2) == ");") 
      {
        $line = str_replace(");","}",$line);
        $inblock=0;
      }

      $sline = strstr($line, "\"");
      $lastchunk = strrchr($line,"\"");
      if ($sline && $lastchunk && strlen($sline) != strlen($lastchunk))
      {
        $beg_restore = substr($line,0,-strlen($sline));

        if (strlen($lastchunk)>1)
           $end_restore = substr($line,1-strlen($lastchunk));
        else $end_restore="";

        $sline = substr($sline,1,strlen($sline)-1-strlen($lastchunk));

        // get rid of chars we can ignore
        $sline=preg_replace("/%\d+/","__TEMP_REPLACE__", $sline);

        $sline=str_replace("\\n","", $sline);
        $sline=str_replace("\"","", $sline);
        $sline=str_replace("$","", $sline);
        $sline=str_replace("%","", $sline);


        // get rid of excess whitespace, especially around commas
        $sline=str_replace("  "," ", $sline);
        $sline=str_replace("  "," ", $sline);
        $sline=str_replace("  "," ", $sline);
        $sline=str_replace(", ",",", $sline);
        $sline=str_replace(" ,",",", $sline);

        $sline=preg_replace("/st\\(([0-9]+)\\)/","FPREG_$1",$sline);


        if (preg_match("/^([0-9]+):/",trim($sline)))
        {
           $d = (int) $sline;
           $a = strstr($sline,":");
           if ($a) $sline = substr($a,1);

           if (isset($btfut[$d]) && $btfut[$d] != "") $thislbl = $btfut[$d]; 
           else $thislbl = "label_" . $labelcnt++;

           $btfut[$d]="";
           $bthist[$d] = $thislbl;

           fputs($out,$thislbl . ":\n");
        }
        
        $sploded = explode(" ",trim($sline));
        if ($sline != "" && count($sploded)>0)
        {        
          $inst = trim($sploded[0]);
          $suffix = "";

          $instline = strstr($sline,$inst); 
          $beg_restore .= substr($sline,0,-strlen($instline));

          $parms = trim(substr($instline,strlen($inst)));

          if ($inst=="j") $inst="jmp";

          //if ($inst == "fdiv" && $parms == "") $inst="fdivr";

          if ($inst != "call" && substr($inst,-2) == "ll") $suffix = "ll";
          else if ($inst != "call" && $inst != "fmul" && substr($inst,-1) == "l") $suffix = "l";
          else if (substr($inst,0,1)=="f" && $inst != "fcos" && $inst != "fsincos" && $inst != "fabs" && $inst != "fchs" && substr($inst,-1) == "s") $suffix = "s";


          if ($suffix != "" && $inst != "jl") $inst = substr($inst,0,-strlen($suffix));

          $parms = preg_replace("/\\((.{2,3}),(.{2,3})\\)/","($1+$2)",$parms);

          $parms=preg_replace("/EEL_F_SUFFIX (-?[0-9]+)\\((.*)\\)/","EEL_ASM_TYPE [$2+$1]",$parms);
          $parms=preg_replace("/EEL_F_SUFFIX \\((.*)\\)/","EEL_ASM_TYPE [$1]",$parms);

          if ($inst == "sh" && $suffix == "ll") { $suffix="l"; $inst="shl"; }
          
          if ($suffix == "ll" || ($suffix == "l" && substr($inst,0,1) == "f" && substr($inst,0,2) != "fi")) $suffixstr = "qword ptr ";
          else if ($suffix == "l") $suffixstr = "dword ptr ";
          else if ($suffix == "s") $suffixstr = "dword ptr ";
          else $suffixstr = "";
          $parms=preg_replace("/(-?[0-9]+)\\((.*)\\)/",$suffixstr . "[$2+$1]",$parms);
          $parms=preg_replace("/\\((.*)\\)/",$suffixstr . "[$1]",$parms);


          $parms=str_replace("EEL_F_SUFFIX","EEL_ASM_TYPE", $parms);
          $parms=str_replace("EEL_F_SSTR","EEL_F_SIZE", $parms);

          $plist = explode(",",$parms);
          if (count($plist) > 2) echo "Warning: too many parameters $parms!\n";
          else if (count($plist)==2) 
          {
             $parms = trim($plist[1]) . ", " . trim($plist[0]);
          }
          else
          {
          }

          if ($inst=="fsts") $inst="fstsw";
          if ($inst=="call" && substr($parms,0,1) == "*") $parms=substr($parms,1);
          if (substr($inst,0,1) == "j") 
          {
            if (substr($parms,-1) == "f")
            {
              $d = (int) substr($parms,0,-1);
              if (isset($btfut[$d]) && $btfut[$d] != "") $thislbl = $btfut[$d]; 
              else $btfut[$d] = $thislbl = "label_" . $labelcnt++;
              $parms = $thislbl;
            }
            else if (substr($parms,-1) == "b")
            {
              $d = (int) substr($parms,0,-1);
              if ($bthist[$d]=="") echo "Error resolving label $parms\n";
              $parms = $bthist[$d];
            }
          }
          if (stristr($parms,"[0xfefefefe]"))
          {
            if ($inst == "fmul" || $inst=="fadd" || $inst == "fcomp")
            { 
              if ($inst=="fmul") $hdr="0x0D";
              if ($inst=="fadd") $hdr="0x05";
              if ($inst=="fcomp") $hdr="0x1D";

              fputs($out,"#if EEL_F_SIZE == 8\n");
              fputs($out,"_emit 0xDC; // $inst qword ptr [0xfefefefe]\n");
              fputs($out,"_emit $hdr;\n");
              fputs($out,"_emit 0xFE;\n");
              fputs($out,"_emit 0xFE;\n");
              fputs($out,"_emit 0xFE;\n");
              fputs($out,"_emit 0xFE;\n");
              fputs($out,"#else\n");
              fputs($out,"_emit 0xD8; // $inst dword ptr [0xfefefefe]\n");
              fputs($out,"_emit $hdr;\n");
              fputs($out,"_emit 0xFE;\n");
              fputs($out,"_emit 0xFE;\n");
              fputs($out,"_emit 0xFE;\n");
              fputs($out,"_emit 0xFE;\n");
              fputs($out,"#endif\n");
              $nowrite=1;
            }
          }
           

          $sline = $inst;
          if ($parms !="") $sline .= " "  . $parms;
          $sline .=  ";";

        }

        $sline=preg_replace("/FPREG_([0-9]+)/","st($1)",$sline);
        $line = $beg_restore . $sline . $end_restore;

      }


    }
  }

  if (!$nowrite)
  {
    if (strstr($line,"__TEMP_REPLACE__"))
    {
      $a = strstr($line,"//REPLACE=");
      if ($a === false) die ("__TEMP_REPLACE__ found, no REPLACE=\n");
      $line=str_replace("__TEMP_REPLACE__",substr($a,10),$line);
    }
    fputs($out,$line . "\n");
  }
}
 
if ($inblock) echo "Error (ended in __asm__ block???)\n";


fclose($in);
fclose($out);

};

process_file("asm-nseel-x86-gcc.c" , "asm-nseel-x86-msvc.c");
// process_file("asm-miscfunc-x86-gcc.c" , "asm-miscfunc-x86-msvc.c");
//process_file("asm-megabuf-x86-gcc.c" , "asm-megabuf-x86-msvc.c");

?>
