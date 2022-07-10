(*$l-*)
{ [saf] This program obtained from http://www.fourmilab.ch/fbench/. It is
  documented extensively in http://www.fourmilab.ch/fbench/fbench.html.
  It was coded by Mr. John Walker. }
{

        John Walker's Floating Point Benchmark, derived from...

        Marinchip Interactive Lens Design System

                                     John Walker   December 1980

        By John Walker
           http://www.fourmilab.ch/

        This program may be used, distributed, and modified freely as
        long as the origin information is preserved.

        This is a complete optical design raytracing algorithm,
        stripped of its user interface and recast into Pascal. It not
        only determines execution speed on an extremely floating point
        (including trig function) intensive real-world application, it
        allows checking accuracy on an algorithm that is exquisitely
        sensitive to errors.  The performance of this program is
        typically far more sensitive to changes in the efficiency of
        the trigonometric library routines than the average floating
        point program.

        Ported from the Ada language implementation in September 2007
        by John Walker.
}

program fbench(input, output);

    const
        OUTER = 100{5753};
	    INNER = 100{5753};
        maxXsurfaces = 10;
        currentXsurfaces = 4;
        clearXaperture = 4.0;

    type
        AxialXIncidence = ( MarginalXRay, ParaxialXRay );

        { Wavelengths of standard spectral lines in Angstroms
                 (Not all are used in this program) }

        SpectralXLineXName = (
            AXline, BXline, CXline, DXline, EXline, FXline,
            GprimeXline, HXline
        );

        SurfaceXProperty = (
            CurvatureXRadius,       { Radius of curvature }
                                    {   (+ if convex to light source }
            IndexXOfXRefraction,    { Index of refraction (1 for air space }
            Dispersion,             { Dispersion (Abbe number (V)) }
            EdgeXThickness          { Edge thickness (0 for last surface) }
        );

    var
        paraxial: AxialXIncidence;
        aberrXlspher, aberrXosc, aberrXlchrom,
        maxXlspher, maxXosc, maxXlchrom,
        radiusXofXcurvature, objectXdistance, rayXheight,
        axisXslopeXangle, fromXindex, toXindex,
        odXcline, odXfline: real;

        testcase: array[1..4, SurfaceXProperty] of real;
        spectralXline: array[SpectralXLineXName] of real;
        s: array[1..maxXsurfaces, SurfaceXProperty] of real;
        odXsa: array[AxialXIncidence, 0..1] of real;

        i, itercount, thousand: integer;
        sp: SurfaceXProperty;
        p: AxialXIncidence;

    {  The arcsin function defined in terms of arctan and sqrt  }

    function arcsin(x: real) : real;
        begin
            arcsin := arctan(x / sqrt(1 - sqr(x)));
        end;

    {  The tan function defined in terms of sin and cos  }

    function tan(x: real) : real;
        begin
            tan := sin(x) / cos(x);
        end;

    {  The cot function defined in terms of tan  }

    function cot(x: real) : real;
        begin
            cot := 1.0 / tan(x);
        end;

    procedure initialise;
    begin

        {  Wavelengths of standard spectral lines in Angstroms
           Not all are used in this program) }

        spectralXline[AXline] := 7621.0;         { A }
        spectralXline[BXline] := 6869.955;       { B }
        spectralXline[CXline] := 6562.816;       { C }
        spectralXline[DXline] := 5895.944;       { D }
        spectralXline[EXline] := 5269.557;       { E }
        spectralXline[FXline] := 4861.344;       { F }
        spectralXline[GprimeXline] := 4340.477;  { G' }
        spectralXline[HXline] := 3968.494;       { H }

        { The  test case used in this program is the design for a 4
          inch f/12 achromatic telescope objective used as the example
          in Wyld's classic work on ray tracing by hand, given in
          Amateur Telescope Making, Volume 3 (Volume 2 in the 1996
          reprint edition). }

        testcase[1][CurvatureXRadius] := 27.05;
        testcase[1][IndexXOfXRefraction] := 1.5137;
        testcase[1][Dispersion] := 63.6;
        testcase[1][EdgeXThickness] := 0.52;

        testcase[2][CurvatureXRadius] := -16.68;
        testcase[2][IndexXOfXRefraction] := 1;
        testcase[2][Dispersion] := 0;
        testcase[2][EdgeXThickness] := 0.138;

        testcase[3][CurvatureXRadius] := -16.68;
        testcase[3][IndexXOfXRefraction] := 1.6164;
        testcase[3][Dispersion] := 36.7;
        testcase[3][EdgeXThickness] := 0.38;

        testcase[4][CurvatureXRadius] := -78.1;
        testcase[4][IndexXOfXRefraction] := 1;
        testcase[4][Dispersion] := 0;
        testcase[4][EdgeXThickness] := 0;
    end;


    {     Calculate passage through surface

          If the variable paraxial is ParaxialXRay, the trace through the
          surface will be done using the paraxial approximations.
          Otherwise, the normal trigonometric trace will be done.

          This subroutine takes the following global inputs:

          radiusXofXcurvature     Radius of curvature of surface
                                  being crossed.  If 0, surface is
                                  plane.

          objectXdistance         Distance of object focus from
                                  lens vertex.  If 0, incoming
                                  rays are parallel and
                                  the following must be specified:

          rayXheight              Height of ray from axis.  Only
                                  relevant if $objectXdistance == 0

          axisXslopeXangle        Angle incoming ray makes with axis
                                  at intercept

          fromXindex              Refractive index of medium being left

          toXindex                Refractive index of medium being
                                  entered.

          The outputs are the following global variables:

          objectXdistance         Distance from vertex to object focus
                                  after refraction.

          axisXslopeXangle        Angle incoming ray makes with axis
                                  at intercept after refraction. }

        procedure transitXsurface;
        var
            iang,                   {  Incidence angle  }
            rang,                   {  Refraction angle  }
            iangXsin,               {  Incidence angle sin  }
            rangXsin,               {  Refraction angle sin  }
            oldXaxisXslopeXangle, sagitta : real;
        begin
            if paraxial = ParaxialXRay then begin
                if radiusXofXcurvature <> 0.0 then begin
                    if objectXdistance = 0.0  then begin
                        axisXslopeXangle := 0.0;
                        iangXsin := rayXheight / radiusXofXcurvature;
                    end else begin
                        iangXsin := ((objectXdistance -
                        radiusXofXcurvature) / radiusXofXcurvature) *
                        axisXslopeXangle;
                    end;
                    rangXsin := (fromXindex / toXindex) * iangXsin;
                    oldXaxisXslopeXangle := axisXslopeXangle;
                    axisXslopeXangle := axisXslopeXangle +
                        iangXsin - rangXsin;
                    if objectXdistance <> 0.0 then begin
                        rayXheight := objectXdistance * oldXaxisXslopeXangle;
                    end;
                    objectXdistance := rayXheight / axisXslopeXangle;
                end else begin
                    objectXdistance := objectXdistance * (toXindex / fromXindex);
                    axisXslopeXangle := axisXslopeXangle * (fromXindex / toXindex);
                end;
            end else begin
                if radiusXofXcurvature <> 0.0 then begin
                    if objectXdistance = 0.0 then begin
                        axisXslopeXangle := 0.0;
                        iangXsin := rayXheight / radiusXofXcurvature;
                    end else begin
                        iangXsin := ((objectXdistance -
                            radiusXofXcurvature) / radiusXofXcurvature) *
                            Sin(axisXslopeXangle);
                    end;
                    iang := Arcsin(iangXsin);
                    rangXsin := (fromXindex / toXindex) * iangXsin;
                    oldXaxisXslopeXangle := axisXslopeXangle;
                    axisXslopeXangle := axisXslopeXangle +
                        iang - Arcsin(rangXsin);
                    sagitta := Sin((oldXaxisXslopeXangle + iang) / 2.0);
                    sagitta := 2.0 * radiusXofXcurvature * sagitta * sagitta;
                    objectXdistance := ((radiusXofXcurvature *
                        Sin(oldXaxisXslopeXangle + iang)) *
                        Cot(axisXslopeXangle)) + sagitta;
                end else begin
                    rang := -Arcsin((fromXindex / toXindex) *
                        Sin(axisXslopeXangle));
                    objectXdistance := objectXdistance * ((toXindex *
                        Cos(-rang)) / (fromXindex *
                        Cos(axisXslopeXangle)));
                    axisXslopeXangle := -rang;
                end;
            end;
        end;


        {  Perform ray trace in specific spectral line  }

        procedure traceXline (line : SpectralXLineXName; rayXh : real);
        var
            i: integer;
        begin
            objectXdistance := 0.0;
            rayXheight := rayXh;
            fromXindex := 1.0;

            for i := 1 to currentXsurfaces do begin
                radiusXofXcurvature := s[i, CurvatureXRadius];
                toXindex := s[i, IndexXOfXRefraction];
                if toXindex > 1.0 then begin
                    toXindex := toXindex + ((spectralXline[DXline] -
                        spectralXline[line]) /
                        (spectralXline[CXline] - spectralXline[FXline])) *
                        ((s[i, IndexXOfXRefraction] - 1.0) / s[i, Dispersion]);
                end;
                transitXsurface;
                fromXindex := toXindex;
                if i < currentXsurfaces then begin
                    objectXdistance := objectXdistance - s[i, EdgeXThickness];
                end;
            end;
        end;

    {  Main program  }

    begin

        {  Load test case into working array  }

        initialise;
        for i := 1 to currentXsurfaces do begin
            for sp := CurvatureXRadius to EdgeXThickness do begin
                s[i, sp] := testcase[i, sp];
            end;
        end;

        write('Press return to begin benchmark: ');
        readln;

        {  Timing begins here  }

        for itercount := 1 to OUTER do begin
            for thousand := 1 to INNER do begin
                for p := MarginalXRay to ParaxialXRay do begin

                    {  Do main trace in D light  }

                    paraxial := p;
                    traceXline(DXline, clearXaperture / 2.0);
                    odXsa[paraxial, 0] := objectXdistance;
                    odXsa[paraxial, 1] := axisXslopeXangle;
                end;
                paraxial := MarginalXRay;

                {  Trace marginal ray in C  }

                traceXline(CXline, clearXaperture / 2.0);
                odXcline := objectXdistance;

                {  Trace marginal ray in F  }

                traceXline(FXline, clearXaperture / 2.0);
                odXfline := objectXdistance;

                aberrXlspher := odXsa[ParaxialXRay, 0] - odXsa[MarginalXRay, 0];
                aberrXosc := 1.0 - (odXsa[ParaxialXRay, 0] * odXsa[ParaxialXRay, 1]) /
                   (Sin(odXsa[MarginalXRay, 1]) * odXsa[MarginalXRay, 0]);
                aberrXlchrom := odXfline - odXcline;
                maxXlspher := Sin(odXsa[MarginalXRay, 1]);

                {  D light  }

                maxXlspher := 0.0000926 / (maxXlspher * maxXlspher);
                maxXosc := 0.0025;
                maxXlchrom := maxXlspher;
            end;
        end;

        {  Timing ends here  }

        write('Stop the timer: ');
        readln;
        writeln;

        writeln('   Marginal ray   ', odXsa[MarginalXRay, 0]:21:11,
            '  ', odXsa[MarginalXRay, 1]:14:11);
        writeln('   Paraxial ray   ', odXsa[ParaxialXRay, 0]:21:11,
            '  ', odXsa[ParaxialXRay, 1]:14:11);
        writeln('Longitudinal spherical aberration:      ', aberrXlspher:16:11);
        writeln('    (Maximum permissible):              ', maxXlspher:16:11);
        writeln('Offense against sine condition (coma):  ', aberrXosc:16:11);
        writeln('    (Maximum permissible):              ', maxXosc:16:11);
        writeln('Axial chromatic aberration:             ', aberrXlchrom:16:11);
        writeln('    (Maximum permissible):              ', maxXlchrom:16:11);

    end.
