//>>built
define("dojox/jsonPath/query",["dojo/_base/kernel","dojo/_base/lang"],function(_1,_2){
var _3=function(_4,_5,_6){
if(!_6){
_6={};
}
var _7=[];
function _8(i){
return _7[i];
};
var _9=_8.name;
if(_6.resultType=="PATH"&&_6.evalType=="RESULT"){
throw Error("RESULT based evaluation not supported with PATH based results");
}
var P={resultType:_6.resultType||"VALUE",normalize:function(_a){
var _b=[];
_a=_a.replace(/'([^']|'')*'/g,function(t){
return _9+"("+(_7.push(eval(t))-1)+")";
});
var ll=-1;
while(ll!=_b.length){
ll=_b.length;
_a=_a.replace(/(\??\([^\(\)]*\))/g,function($0){
return "#"+(_b.push($0)-1);
});
}
_a=_a.replace(/[\['](#[0-9]+)[\]']/g,"[$1]").replace(/'?\.'?|\['?/g,";").replace(/;;;|;;/g,";..;").replace(/;$|'?\]|'$/g,"");
ll=-1;
while(ll!=_a){
ll=_a;
_a=_a.replace(/#([0-9]+)/g,function($0,$1){
return _b[$1];
});
}
return _a.split(";");
},asPaths:function(_c){
var i,j,p,x,n;
for(j=0;j<_c.length;j++){
p="$";
x=_c[j];
for(i=1,n=x.length;i<n;i++){
p+=/^[0-9*]+$/.test(x[i])?("["+x[i]+"]"):("['"+x[i]+"']");
}
_c[j]=p;
}
return _c;
},exec:function(_d,_e,rb){
var _f=["$"];
var _10=rb?_e:[_e];
var _11=[_f];
function add(v,p,def){
if(v&&v.hasOwnProperty(p)&&P.resultType!="VALUE"){
_11.push(_f.concat([p]));
}
if(def){
_10=v[p];
}else{
if(v&&v.hasOwnProperty(p)){
_10.push(v[p]);
}
}
};
function _12(v){
_10.push(v);
_11.push(_f);
P.walk(v,function(i){
if(typeof v[i]==="object"){
var _13=_f;
_f=_f.concat(i);
_12(v[i]);
_f=_13;
}
});
};
function _14(loc,val){
if(val instanceof Array){
var len=val.length,_15=0,end=len,_16=1;
loc.replace(/^(-?[0-9]*):(-?[0-9]*):?(-?[0-9]*)$/g,function($0,$1,$2,$3){
_15=parseInt($1||_15,10);
end=parseInt($2||end,10);
_16=parseInt($3||_16,10);
});
_15=(_15<0)?Math.max(0,_15+len):Math.min(len,_15);
end=(end<0)?Math.max(0,end+len):Math.min(len,end);
var i;
for(i=_15;i<end;i+=_16){
add(val,i);
}
}
};
function _17(str,loc){
var i=loc.match(/^_str\(([0-9]+)\)$/);
return i?_7[i[1]]:str;
};
function _18(val,loc){
if(/^\(.*?\)$/.test(loc)){
add(val,P.eval(loc,val),rb);
}else{
if(loc==="*"){
P.walk(val,rb&&val instanceof Array?function(i){
P.walk(val[i],function(j){
add(val[i],j);
});
}:function(i){
add(val,i);
});
}else{
if(loc===".."){
_12(val);
}else{
if(/,/.test(loc)){
var s,n,i;
for(s=loc.split(/'?,'?/),i=0,n=s.length;i<n;i++){
add(val,_17(s[i],loc));
}
}else{
if(/^\?\(.*?\)$/.test(loc)){
P.walk(val,function(i){
if(P.eval(loc.replace(/^\?\((.*?)\)$/,"$1"),val[i])){
add(val,i);
}
});
}else{
if(/^(-?[0-9]*):(-?[0-9]*):?([0-9]*)$/.test(loc)){
_14(loc,val);
}else{
loc=_17(loc,loc);
if(rb&&val instanceof Array&&!/^[0-9*]+$/.test(loc)){
P.walk(val,function(i){
add(val[i],loc);
});
}else{
add(val,loc,rb);
}
}
}
}
}
}
}
};
var loc,_19;
while(_d.length){
loc=_d.shift();
if((_e=_10)===null||_e===undefined){
return _e;
}
_10=[];
_19=_11;
_11=[];
if(rb){
_18(_e,loc);
}else{
P.walk(_e,function(i){
_f=_19[i]||_f;
_18(_e[i],loc);
});
}
}
if(P.resultType=="BOTH"){
_11=P.asPaths(_11);
var _1a=[];
var i;
for(i=0;i<_11.length;i++){
_1a.push({path:_11[i],value:_10[i]});
}
return _1a;
}
return P.resultType=="PATH"?P.asPaths(_11):_10;
},walk:function(val,f){
var i,n,m;
if(val instanceof Array){
for(i=0,n=val.length;i<n;i++){
if(i in val){
f(i);
}
}
}else{
if(typeof val==="object"){
for(m in val){
if(val.hasOwnProperty(m)){
f(m);
}
}
}
}
},"eval":function(x,v){
try{
return _4&&v&&eval(x.replace(/@/g,"v"));
}
catch(e){
throw new SyntaxError("jsonPath: "+e.message+": "+x.replace(/@/g,"v").replace(/\^/g,"_a"));
}
}};
if(_5&&_4){
return P.exec(P.normalize(_5).slice(1),_4,_6.evalType=="RESULT");
}
return false;
};
if(!_1.isAsync){
var obj=_2.getObject("dojox.jsonPath",true);
obj.query=_3;
}
return _3;
});
