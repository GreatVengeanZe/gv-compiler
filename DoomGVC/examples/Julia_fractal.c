extern void putchar(int c);

int main(){int r=0,c=0,i=0;double w=88.0,h=40.0;int m=56;
/*                                                     */
do{/*                                                  #*/
if((r%17)==16){r++;continue;}/*                        #+.*/
c=0;while(1){if(c>=w)break;/*                          ### #*/
double x0=(c/w)*3.2-1.9;double y0=(r/h)*2.2-1.1;/*     ...#@*/
double x=x0,y=y0;/*                                 #@#+*# #*/
for(i=0;i<m;i++){double xx=x*x,yy=y*y;/*          .. ####++#....*++ .#*/
if(xx+yy>4.0)break;/*                              .#.####+.. @####@+##*/
if(i<3&&xx+yy<0.03)continue;/*                     *++*##*#@@########*##*/
y=2.0*x*y+0.156;x=xx-yy-0.8;}/*               + #########* ########## #*/
if((r%2)==1&&(c%11)==0){putchar(32);c++;continue;}/*################*/
if(i>=m)putchar(35);/*                              #################*/
else if(i>(m*4)/5)putchar(64);/*                 ########## **@###++#*/
else if(i>(m*3)/5)putchar(42);/*             ########## #@++....++ ++#*/
else if(i>(m*2)/5)putchar(43);/*          ###########+......##@#*#*#*/
else if(i>m/5)putchar(46);/*            .*#######@ *#@....*#* @*#*/
else putchar(32);c++;}/*                #@ ++++++#*## ###@*/
putchar(10);r++;}while(r<h);/*              #########+ *#*/
return 0;}/*                                  #+#+@+*/