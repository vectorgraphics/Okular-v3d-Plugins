/* bound.cc
 * Bézier bounding-box computation extracted from path3.cc.
 */

#include <cfloat>
#include <cmath>
#include "bound.h"
#include "triple.h"
#include "bbox.h"

namespace camp {

const double Fuzz = sqrt(1000.0 * DBL_EPSILON);
const unsigned maxdepth = DBL_MANT_DIG;

namespace run {
    double norm(double *a, size_t n) {
        double result = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double v = a[i] < 0 ? -a[i] : a[i];
            if (v > result) result = v;
        }
        return result;
    }
}

template<class T>
struct Split {
  T m0,m1,m2,m3,m4,m5;
  Split(T z0, T c0, T c1, T z1) {
    m0=0.5*(z0+c0);
    m1=0.5*(c0+c1);
    m2=0.5*(c1+z1);
    m3=0.5*(m0+m1);
    m4=0.5*(m1+m2);
    m5=0.5*(m3+m4);
  }
};

double cornerbound(double *P, double (*m)(double, double)) {
  double b=m(P[0],P[3]);
  b=m(b,P[12]);
  return m(b,P[15]);
}

double controlbound(double *P, double (*m)(double, double)) {
  double b=m(P[1],P[2]);
  b=m(b,P[4]);
  b=m(b,P[5]);
  b=m(b,P[6]);
  b=m(b,P[7]);
  b=m(b,P[8]);
  b=m(b,P[9]);
  b=m(b,P[10]);
  b=m(b,P[11]);
  b=m(b,P[13]);
  return m(b,P[14]);
}

double bound(double *P, double (*m)(double, double), double b,
             double fuzz, int depth) {
  b=m(b,cornerbound(P,m));
  if(m(-1.0,1.0)*(b-controlbound(P,m)) >= -fuzz || depth == 0)
    return b;

  --depth;
  fuzz *= 2;

  Split<double> c0(P[0],P[1],P[2],P[3]);
  Split<double> c1(P[4],P[5],P[6],P[7]);
  Split<double> c2(P[8],P[9],P[10],P[11]);
  Split<double> c3(P[12],P[13],P[14],P[15]);

  Split<double> c4(P[12],P[8],P[4],P[0]);
  Split<double> c5(c3.m0,c2.m0,c1.m0,c0.m0);
  Split<double> c6(c3.m3,c2.m3,c1.m3,c0.m3);
  Split<double> c7(c3.m5,c2.m5,c1.m5,c0.m5);
  Split<double> c8(c3.m4,c2.m4,c1.m4,c0.m4);
  Split<double> c9(c3.m2,c2.m2,c1.m2,c0.m2);
  Split<double> c10(P[15],P[11],P[7],P[3]);

  double s0[]={c4.m5,c5.m5,c6.m5,c7.m5,c4.m3,c5.m3,c6.m3,c7.m3,
               c4.m0,c5.m0,c6.m0,c7.m0,P[12],c3.m0,c3.m3,c3.m5};
  b=bound(s0,m,b,fuzz,depth);
  double s1[]={P[0],c0.m0,c0.m3,c0.m5,c4.m2,c5.m2,c6.m2,c7.m2,
               c4.m4,c5.m4,c6.m4,c7.m4,c4.m5,c5.m5,c6.m5,c7.m5};
  b=bound(s1,m,b,fuzz,depth);
  double s2[]={c0.m5,c0.m4,c0.m2,P[3],c7.m2,c8.m2,c9.m2,c10.m2,
               c7.m4,c8.m4,c9.m4,c10.m4,c7.m5,c8.m5,c9.m5,c10.m5};
  b=bound(s2,m,b,fuzz,depth);
  double s3[]={c7.m5,c8.m5,c9.m5,c10.m5,c7.m3,c8.m3,c9.m3,c10.m3,
               c7.m0,c8.m0,c9.m0,c10.m0,c3.m5,c3.m4,c3.m2,P[15]};
  return bound(s3,m,b,fuzz,depth);
}

template<class T>
struct Splittri {
  T l003,p102,p012,p201,p111,p021,r300,p210,p120,u030;
  T u021,u120;
  T p033,p231,p330;
  T p123;
  T l012,p312,r210,l102,p303,r201;
  T u012,u210,l021,p4xx,r120,px4x,pxx4,l201,r102;
  T l210,r012,l300;
  T r021,u201,r030;
  T u102,l120,l030;
  T l111,r111,u111,c111;

  Splittri(const T *p) {
    l003=p[0]; p102=p[1]; p012=p[2]; p201=p[3]; p111=p[4];
    p021=p[5]; r300=p[6]; p210=p[7]; p120=p[8]; u030=p[9];

    u021=0.5*(u030+p021); u120=0.5*(u030+p120);
    p033=0.5*(p021+p012); p231=0.5*(p120+p111); p330=0.5*(p120+p210);
    p123=0.5*(p012+p111);

    l012=0.5*(p012+l003); p312=0.5*(p111+p201); r210=0.5*(p210+r300);
    l102=0.5*(l003+p102); p303=0.5*(p102+p201); r201=0.5*(p201+r300);

    u012=0.5*(u021+p033); u210=0.5*(u120+p330);
    l021=0.5*(p033+l012); p4xx=0.5*p231+0.25*(p111+p102);
    r120=0.5*(p330+r210); px4x=0.5*p123+0.25*(p111+p210);
    pxx4=0.25*(p021+p111)+0.5*p312;
    l201=0.5*(l102+p303); r102=0.5*(p303+r201);

    l210=0.5*(px4x+l201); r012=0.5*(px4x+r102); l300=0.5*(l201+r102);
    r021=0.5*(pxx4+r120); u201=0.5*(u210+pxx4); r030=0.5*(u210+r120);
    u102=0.5*(u012+p4xx); l120=0.5*(l021+p4xx); l030=0.5*(u012+l021);

    l111=0.5*(p123+l102); r111=0.5*(p312+r210);
    u111=0.5*(u021+p231); c111=0.25*(p033+p330+p303+p111);
  }
};

double cornerboundtri(double *P, double (*m)(double, double)) {
  double b=m(P[0],P[6]);
  return m(b,P[9]);
}

double controlboundtri(double *P, double (*m)(double, double)) {
  double b=m(P[1],P[2]);
  b=m(b,P[3]);
  b=m(b,P[4]);
  b=m(b,P[5]);
  b=m(b,P[7]);
  return m(b,P[8]);
}

double boundtri(double *P, double (*m)(double, double), double b,
                double fuzz, int depth) {
  b=m(b,cornerboundtri(P,m));
  if(m(-1.0,1.0)*(b-controlboundtri(P,m)) >= -fuzz || depth == 0)
    return b;

  --depth;
  fuzz *= 2;

  Splittri<double> s(P);

  double l[]={s.l003,s.l102,s.l012,s.l201,s.l111,
              s.l021,s.l300,s.l210,s.l120,s.l030};
  b=boundtri(l,m,b,fuzz,depth);

  double r[]={s.l300,s.r102,s.r012,s.r201,s.r111,
              s.r021,s.r300,s.r210,s.r120,s.r030};
  b=boundtri(r,m,b,fuzz,depth);

  double u[]={s.l030,s.u102,s.u012,s.u201,s.u111,
              s.u021,s.r030,s.u210,s.u120,s.u030};
  b=boundtri(u,m,b,fuzz,depth);

  double c[]={s.r030,s.u201,s.r021,s.u102,s.c111,
              s.r012,s.l030,s.l120,s.l210,s.l300};
  return boundtri(c,m,b,fuzz,depth);
}

double ratiobound_curve(triple z0, triple c0, triple c1, triple z1,
                  double (*m)(double, double),
                  double (*f)(const triple&)) {
  double MX=m(m(m(-z0.getx(),-c0.getx()),-c1.getx()),-z1.getx());
  double MY=m(m(m(-z0.gety(),-c0.gety()),-c1.gety()),-z1.gety());
  double Z=m(m(m(z0.getz(),c0.getz()),c1.getz()),z1.getz());
  double MZ=m(m(m(-z0.getz(),-c0.getz()),-c1.getz()),-z1.getz());
  return m(f(triple(-MX,-MY,Z)),f(triple(-MX,-MY,-MZ)));
}

double bound(triple z0, triple c0, triple c1, triple z1,
             double (*m)(double, double),
             double (*f)(const triple&), double b, double fuzz, int depth) {
  b=m(b,m(f(z0),f(z1)));
  if(m(-1.0,1.0)*(b-ratiobound_curve(z0,c0,c1,z1,m,f)) >= -fuzz || depth == 0)
    return b;

  --depth;
  fuzz *= 2;

  triple m0=0.5*(z0+c0);
  triple m1=0.5*(c0+c1);
  triple m2=0.5*(c1+z1);
  triple m3=0.5*(m0+m1);
  triple m4=0.5*(m1+m2);
  triple m5=0.5*(m3+m4);

  b=bound(z0,m0,m3,m5,m,f,b,fuzz,depth);
  return bound(m5,m4,m2,z1,m,f,b,fuzz,depth);
}

} // namespace camp

