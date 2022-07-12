#ifndef IMPORT_AS_MEMBER_H
#define IMPORT_AS_MEMBER_H

struct __attribute__((swift_name("Struct1"))) IAMStruct1 {
  double x, y, z;
};

extern double IAMStruct1GlobalVar
    __attribute__((swift_name("Struct1.globalVar")));

extern struct IAMStruct1 IAMStruct1CreateSimple(double value)
    __attribute__((swift_name("Struct1.init(value:)")));

extern struct IAMStruct1 IAMStruct1CreateSpecialLabel(void)
    __attribute__((swift_name("Struct1.init(specialLabel:)")));

extern struct IAMStruct1 IAMStruct1Invert(struct IAMStruct1 s)
    __attribute__((swift_name("Struct1.inverted(self:)")));

extern void IAMStruct1InvertInPlace(struct IAMStruct1 *s)
    __attribute__((swift_name("Struct1.invert(self:)")));

extern struct IAMStruct1 IAMStruct1Rotate(const struct IAMStruct1 *s,
                                          double radians)
    __attribute__((swift_name("Struct1.translate(self:radians:)")));

extern struct IAMStruct1 IAMStruct1Scale(struct IAMStruct1 s,
                                         double radians)
    __attribute__((swift_name("Struct1.scale(self:_:)")));

extern double IAMStruct1GetRadius(const struct IAMStruct1 *s)
    __attribute__((swift_name("getter:Struct1.radius(self:)")));

extern void IAMStruct1SetRadius(struct IAMStruct1 s, double radius)
    __attribute__((swift_name("setter:Struct1.radius(self:_:)")));

extern double IAMStruct1GetAltitude(struct IAMStruct1 s)
    __attribute__((swift_name("getter:Struct1.altitude(self:)")));

extern void IAMStruct1SetAltitude(struct IAMStruct1 *s, double altitude)
    __attribute__((swift_name("setter:Struct1.altitude(self:_:)")));

extern double IAMStruct1GetMagnitude(struct IAMStruct1 s)
    __attribute__((swift_name("getter:Struct1.magnitude(self:)")));

extern int IAMStruct1StaticMethod(void)
    __attribute__((swift_name("Struct1.staticMethod()")));
extern int IAMStruct1StaticGetProperty(void)
    __attribute__((swift_name("getter:Struct1.property()")));
extern int IAMStruct1StaticSetProperty(int i)
    __attribute__((swift_name("setter:Struct1.property(i:)")));
extern int IAMStruct1StaticGetOnlyProperty(void)
    __attribute__((swift_name("getter:Struct1.getOnlyProperty()")));

extern void IAMStruct1SelfComesLast(double x, struct IAMStruct1 s)
    __attribute__((swift_name("Struct1.selfComesLast(x:self:)")));
extern void IAMStruct1SelfComesThird(int a, float b, struct IAMStruct1 s, double x)
    __attribute__((swift_name("Struct1.selfComesThird(a:b:self:x:)")));

#endif // IMPORT_AS_MEMBER_H
