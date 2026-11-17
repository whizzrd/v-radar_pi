GribRecord.{h,cpp} and GribRecordSet.h are copied from the OpenCPN grib_pi plugin (via shipdriver_pi),
GPL-2.0-or-later, (c) 2010 David S. Register and others. They are needed to read the GribRecordSet
that grib_pi hands to other plugins in its GRIB_TIMELINE_RECORD message; the layout must match
grib_pi 4.1 .. 5.0 (see the version check in v_radar_pi.cpp).
