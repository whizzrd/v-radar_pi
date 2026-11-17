#include "ais_encoder.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

constexpr int kLonNotAvailable = 0x6791AC0;
constexpr int kLatNotAvailable = 0x3412140;

int EncodeLongitude(double lon) {
  if (!std::isfinite(lon) || lon < -180.0 || lon > 180.0) {
    return kLonNotAvailable;
  }
  return static_cast<int>(std::llround(lon * 600000.0));
}

int EncodeLatitude(double lat) {
  if (!std::isfinite(lat) || lat < -90.0 || lat > 90.0) {
    return kLatNotAvailable;
  }
  return static_cast<int>(std::llround(lat * 600000.0));
}

unsigned int EncodeSogTenths(double sog_knots) {
  if (!std::isfinite(sog_knots) || sog_knots < 0.0) {
    return 1023U;
  }
  if (sog_knots > 102.2) {
    return 1022U;
  }
  return static_cast<unsigned int>(std::llround(sog_knots * 10.0));
}

unsigned int EncodeCogTenths(double cog_degrees) {
  if (!std::isfinite(cog_degrees) || cog_degrees < 0.0 || cog_degrees >= 360.0) {
    return 3600U;
  }
  return static_cast<unsigned int>(std::llround(cog_degrees * 10.0));
}

unsigned int EncodeTrueHeading(int heading) {
  if (heading < 0 || heading >= 360) {
    return 511U;
  }
  return static_cast<unsigned int>(heading);
}

char NormalizeAisNameChar(unsigned char ch) {
  if (ch >= 'a' && ch <= 'z') {
    ch = static_cast<unsigned char>(std::toupper(ch));
  }
  if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
    return static_cast<char>(ch);
  }
  switch (ch) {
    case ' ':
    case '-':
    case '/':
      return static_cast<char>(ch);
    default:
      return ' ';
  }
}

}  // namespace

std::string AisEncoder::EncodeAivdmMessage21(const VirtualAtonReport& report) {
  std::string bits;
  bits.reserve(272);

  AppendUnsigned(bits, 21, 6);
  AppendUnsigned(bits, 0, 2);
  AppendUnsigned(bits, static_cast<unsigned int>(report.mmsi), 30);
  AppendUnsigned(bits, static_cast<unsigned int>(report.aid_type), 5);
  AppendSixBitText(bits, report.name, 20);
  AppendUnsigned(bits, report.position_accuracy_high ? 1U : 0U, 1);
  AppendSigned(bits, EncodeLongitude(report.longitude), 28);
  AppendSigned(bits, EncodeLatitude(report.latitude), 27);
  AppendUnsigned(bits, 0, 9);
  AppendUnsigned(bits, 0, 9);
  AppendUnsigned(bits, 0, 6);
  AppendUnsigned(bits, 0, 6);
  AppendUnsigned(bits, 1, 4);
  AppendUnsigned(bits, 60, 6);
  AppendUnsigned(bits, report.off_position ? 1U : 0U, 1);
  AppendUnsigned(bits, 0, 8);
  AppendUnsigned(bits, 0, 1);
  AppendUnsigned(bits, report.virtual_aid ? 1U : 0U, 1);
  AppendUnsigned(bits, report.assigned_mode ? 1U : 0U, 1);
  AppendUnsigned(bits, 0, 1);

  int fill_bits = 0;
  const std::string payload = BitsToPayload(bits, fill_bits);

  std::ostringstream body;
  body << "AIVDM,1,1,,A," << payload << "," << fill_bits;

  return "!" + body.str() + "*" + NmeaChecksum(body.str());
}

std::string AisEncoder::EncodeAivdmMessage1(const VirtualVesselReport& report) {
  std::string bits;
  bits.reserve(168);

  AppendUnsigned(bits, 1, 6);
  AppendUnsigned(bits, 0, 2);
  AppendUnsigned(bits, static_cast<unsigned int>(report.mmsi), 30);
  AppendUnsigned(bits, 0, 4);
  AppendUnsigned(bits, 128, 8);
  AppendUnsigned(bits, EncodeSogTenths(report.sog_knots), 10);
  AppendUnsigned(bits, report.position_accuracy_high ? 1U : 0U, 1);
  AppendSigned(bits, EncodeLongitude(report.longitude), 28);
  AppendSigned(bits, EncodeLatitude(report.latitude), 27);
  AppendUnsigned(bits, EncodeCogTenths(report.cog_degrees), 12);
  AppendUnsigned(bits, EncodeTrueHeading(report.true_heading), 9);
  AppendUnsigned(bits, 60, 6);
  AppendUnsigned(bits, 0, 2);
  AppendUnsigned(bits, 0, 3);
  AppendUnsigned(bits, 0, 1);
  AppendUnsigned(bits, 0, 19);

  int fill_bits = 0;
  const std::string payload = BitsToPayload(bits, fill_bits);

  std::ostringstream body;
  body << "AIVDM,1,1,,A," << payload << "," << fill_bits;

  return "!" + body.str() + "*" + NmeaChecksum(body.str());
}

std::string AisEncoder::EncodeAivdmMessage5(const VirtualVesselReport& report) {
  std::string bits;
  bits.reserve(424);

  AppendUnsigned(bits, 5, 6);
  AppendUnsigned(bits, 0, 2);
  AppendUnsigned(bits, static_cast<unsigned int>(report.mmsi), 30);
  AppendUnsigned(bits, 0, 2);
  AppendUnsigned(bits, 0, 30);
  AppendSixBitText(bits, "", 7);
  AppendSixBitText(bits, report.name, 20);
  AppendUnsigned(bits, static_cast<unsigned int>(report.ship_type), 8);
  AppendUnsigned(bits, static_cast<unsigned int>(report.dimension_to_bow), 9);
  AppendUnsigned(bits, static_cast<unsigned int>(report.dimension_to_stern), 9);
  AppendUnsigned(bits, static_cast<unsigned int>(report.dimension_to_port), 6);
  AppendUnsigned(bits,
                 static_cast<unsigned int>(report.dimension_to_starboard), 6);
  AppendUnsigned(bits, 0, 4);
  AppendUnsigned(bits, 0, 4);
  AppendUnsigned(bits, 0, 5);
  AppendUnsigned(bits, 24, 5);
  AppendUnsigned(bits, 60, 6);
  AppendUnsigned(bits, 0, 8);
  AppendSixBitText(bits, "", 20);
  AppendUnsigned(bits, 0, 1);
  AppendUnsigned(bits, 0, 1);

  int fill_bits = 0;
  const std::string payload = BitsToPayload(bits, fill_bits);
  const std::string fragment_one = payload.substr(0, 60);
  const std::string fragment_two = payload.substr(60);

  std::ostringstream body_one;
  body_one << "AIVDM,2,1,0,A," << fragment_one << ",0";
  std::ostringstream body_two;
  body_two << "AIVDM,2,2,0,A," << fragment_two << "," << fill_bits;

  return "!" + body_one.str() + "*" + NmeaChecksum(body_one.str()) + "\n!" +
         body_two.str() + "*" + NmeaChecksum(body_two.str());
}

std::string AisEncoder::EncodeAivdmMessage18(const VirtualVesselReport& report) {
  std::string bits;
  bits.reserve(168);

  AppendUnsigned(bits, 18, 6);
  AppendUnsigned(bits, 0, 2);
  AppendUnsigned(bits, static_cast<unsigned int>(report.mmsi), 30);
  AppendUnsigned(bits, 0, 8);
  AppendUnsigned(bits, EncodeSogTenths(report.sog_knots), 10);
  AppendUnsigned(bits, report.position_accuracy_high ? 1U : 0U, 1);
  AppendSigned(bits, EncodeLongitude(report.longitude), 28);
  AppendSigned(bits, EncodeLatitude(report.latitude), 27);
  AppendUnsigned(bits, EncodeCogTenths(report.cog_degrees), 12);
  AppendUnsigned(bits, EncodeTrueHeading(report.true_heading), 9);
  AppendUnsigned(bits, 60, 6);
  AppendUnsigned(bits, 0, 2);
  AppendUnsigned(bits, report.assigned_mode ? 1U : 0U, 1);
  AppendUnsigned(bits, 0, 1);
  AppendUnsigned(bits, 0, 1);
  AppendUnsigned(bits, 0, 1);
  AppendUnsigned(bits, 0, 1);
  AppendUnsigned(bits, 0, 1);
  AppendUnsigned(bits, 0, 1);
  AppendUnsigned(bits, 0, 20);

  int fill_bits = 0;
  const std::string payload = BitsToPayload(bits, fill_bits);

  std::ostringstream body;
  body << "AIVDM,1,1,,A," << payload << "," << fill_bits;

  return "!" + body.str() + "*" + NmeaChecksum(body.str());
}

std::string AisEncoder::EncodeAivdmMessage24PartA(
    const VirtualVesselReport& report) {
  std::string bits;
  bits.reserve(168);

  AppendUnsigned(bits, 24, 6);
  AppendUnsigned(bits, 0, 2);
  AppendUnsigned(bits, static_cast<unsigned int>(report.mmsi), 30);
  AppendUnsigned(bits, 0, 2);
  AppendSixBitText(bits, report.name, 20);
  AppendUnsigned(bits, 0, 8);

  int fill_bits = 0;
  const std::string payload = BitsToPayload(bits, fill_bits);

  std::ostringstream body;
  body << "AIVDM,1,1,,A," << payload << "," << fill_bits;

  return "!" + body.str() + "*" + NmeaChecksum(body.str());
}

std::string AisEncoder::EncodeAivdmMessage24PartB(
    const VirtualVesselReport& report) {
  std::string bits;
  bits.reserve(168);

  AppendUnsigned(bits, 24, 6);
  AppendUnsigned(bits, 0, 2);
  AppendUnsigned(bits, static_cast<unsigned int>(report.mmsi), 30);
  AppendUnsigned(bits, 1, 2);
  AppendUnsigned(bits, static_cast<unsigned int>(report.ship_type), 8);
  AppendSixBitText(bits, "", 7);
  AppendUnsigned(bits, static_cast<unsigned int>(report.dimension_to_bow), 9);
  AppendUnsigned(bits, static_cast<unsigned int>(report.dimension_to_stern), 9);
  AppendUnsigned(bits, static_cast<unsigned int>(report.dimension_to_port), 6);
  AppendUnsigned(bits,
                 static_cast<unsigned int>(report.dimension_to_starboard), 6);
  AppendSixBitText(bits, "", 6);
  AppendUnsigned(bits, report.assigned_mode ? 1U : 0U, 1);
  AppendUnsigned(bits, 0, 1);

  int fill_bits = 0;
  const std::string payload = BitsToPayload(bits, fill_bits);

  std::ostringstream body;
  body << "AIVDM,1,1,,A," << payload << "," << fill_bits;

  return "!" + body.str() + "*" + NmeaChecksum(body.str());
}

void AisEncoder::AppendUnsigned(std::string& bits, unsigned long long value,
                                int width) {
  for (int i = width - 1; i >= 0; --i) {
    bits.push_back(((value >> i) & 1ULL) ? '1' : '0');
  }
}

void AisEncoder::AppendSigned(std::string& bits, long long value, int width) {
  const unsigned long long mask =
      width == 64 ? ~0ULL : ((1ULL << width) - 1ULL);
  AppendUnsigned(bits, static_cast<unsigned long long>(value) & mask, width);
}

void AisEncoder::AppendSixBitText(std::string& bits, const std::string& text,
                                  int characters) {
  std::string normalized;
  normalized.reserve(text.size());
  for (unsigned char ch : text) {
    normalized.push_back(NormalizeAisNameChar(ch));
  }

  if (normalized.size() < static_cast<size_t>(characters)) {
    normalized.append(static_cast<size_t>(characters) - normalized.size(), ' ');
  } else {
    normalized.resize(characters);
  }
  for (int i = 0; i < characters; ++i) {
    AppendUnsigned(bits, static_cast<unsigned int>(AsciiToSixBit(normalized[i])), 6);
  }
}

int AisEncoder::AsciiToSixBit(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 1;
  if (ch >= '0' && ch <= '9') return ch - '0' + 48;
  switch (ch) {
    case ' ':
      return 32;
    case '@':
      return 0;
    case '-':
      return 45;
    case '/':
      return 47;
    default:
      return 0;
  }
}

std::string AisEncoder::BitsToPayload(const std::string& bits, int& fill_bits) {
  std::string padded = bits;
  fill_bits = (6 - static_cast<int>(bits.size() % 6)) % 6;
  padded.append(fill_bits, '0');

  std::string payload;
  payload.reserve(padded.size() / 6);
  for (size_t offset = 0; offset < padded.size(); offset += 6) {
    int value = 0;
    for (size_t i = 0; i < 6; ++i) {
      value = (value << 1) | (padded[offset + i] == '1' ? 1 : 0);
    }
    payload.push_back(static_cast<char>(value < 40 ? value + 48 : value + 56));
  }
  return payload;
}

std::string AisEncoder::NmeaChecksum(const std::string& sentence_body) {
  unsigned char checksum = 0;
  for (unsigned char ch : sentence_body) {
    checksum ^= ch;
  }

  std::ostringstream out;
  out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
      << static_cast<int>(checksum);
  return out.str();
}
