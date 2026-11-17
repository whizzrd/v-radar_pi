#ifndef V_ATON_AIS_ENCODER_H
#define V_ATON_AIS_ENCODER_H

#include <string>

struct VirtualAtonReport {
  int mmsi = 0;
  int aid_type = 1;
  double latitude = 0.0;
  double longitude = 0.0;
  std::string name;
  bool position_accuracy_high = true;
  bool off_position = false;
  bool virtual_aid = true;
  bool assigned_mode = false;
};

struct VirtualVesselReport {
  int mmsi = 0;
  double latitude = 0.0;
  double longitude = 0.0;
  std::string name;
  int ship_type = 90;
  double sog_knots = 0.0;
  double cog_degrees = 360.0;
  int true_heading = 511;
  int dimension_to_bow = 0;
  int dimension_to_stern = 0;
  int dimension_to_port = 0;
  int dimension_to_starboard = 0;
  bool position_accuracy_high = true;
  bool assigned_mode = false;
};

class AisEncoder {
public:
  static std::string EncodeAivdmMessage21(const VirtualAtonReport& report);
  static std::string EncodeAivdmMessage1(const VirtualVesselReport& report);
  static std::string EncodeAivdmMessage5(const VirtualVesselReport& report);
  static std::string EncodeAivdmMessage18(const VirtualVesselReport& report);
  static std::string EncodeAivdmMessage24PartA(const VirtualVesselReport& report);
  static std::string EncodeAivdmMessage24PartB(const VirtualVesselReport& report);

private:
  static void AppendUnsigned(std::string& bits, unsigned long long value,
                             int width);
  static void AppendSigned(std::string& bits, long long value, int width);
  static void AppendSixBitText(std::string& bits, const std::string& text,
                               int characters);
  static int AsciiToSixBit(char ch);
  static std::string BitsToPayload(const std::string& bits, int& fill_bits);
  static std::string NmeaChecksum(const std::string& sentence_body);
};

#endif
