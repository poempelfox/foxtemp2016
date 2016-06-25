
# $Id: 36_Foxtemp2016viaJeelink.pm $
# This is mostly a modified version of 36_LaCrosse.pm from FHEM.
# You need to put this into /opt/fhem/FHEM/ and to make it work, you'll
# also need to modify 36_JeeLink.pm: to the string "clientsJeeLink" append
# ":Foxtemp2016viaJeelink".

package main;

use strict;
use warnings;
use SetExtensions;

sub Foxtemp2016viaJeelink_Parse($$);


sub Foxtemp2016viaJeelink_Initialize($) {
  my ($hash) = @_;
                       # OK CC 8 247 98 194 159 169 198
  $hash->{'Match'}     = '^\S+\s+CC\s+\d+\s+247\s+\d+\s+\d+\s+\d+\s+\d+\s+\d+\s*$';  # FIXME
  $hash->{'SetFn'}     = "Foxtemp2016viaJeelink_Set";
  ###$hash->{'GetFn'}     = "Foxtemp2016viaJeelink_Get";
  $hash->{'DefFn'}     = "Foxtemp2016viaJeelink_Define";
  $hash->{'UndefFn'}   = "Foxtemp2016viaJeelink_Undef";
  $hash->{'FingerprintFn'}   = "Foxtemp2016viaJeelink_Fingerprint";
  $hash->{'ParseFn'}   = "Foxtemp2016viaJeelink_Parse";
  ###$hash->{'AttrFn'}    = "Foxtemp2016viaJeelink_Attr";
  $hash->{'AttrList'}  = "IODev"
    ." ignore:1,0"
    ." $readingFnAttributes";

  $hash->{'AutoCreate'} = { "Foxtemp2016viaJeelink.*" => { autocreateThreshold => "2:120", FILTER => "%NAME" }};

}

sub Foxtemp2016viaJeelink_Define($$) {
  my ($hash, $def) = @_;
  my @a = split("[ \t][ \t]*", $def);

  if ((int(@a) < 3) || (int(@a) > 3)) {
    my $msg = "wrong syntax: define <name> Foxtemp2016viaJeelink <addr>";
    Log3 undef, 2, $msg;
    return $msg;
  }

  $a[2] =~ m/^(\d{1,3}|0x[\da-f]{2})$/i;
  unless (defined($1)) {
    return "$a[2] is not a valid Foxtemp2016 address";
  }

  my $name = $a[0];
  my $addr;
  if ($a[2] =~ m/0x(.+)/) {
    $addr = $1;
  } else {
    $addr = sprintf("%02x", $a[2]);
  }

  return "Foxtemp2016viaJeelink device $addr already used for $modules{Foxtemp2016viaJeelink}{defptr}{$addr}->{NAME}." if( $modules{Foxtemp2016viaJeelink}{defptr}{$addr} && $modules{Foxtemp2016viaJeelink}{defptr}{$addr}->{NAME} ne $name );

  $hash->{addr} = $addr;

  $modules{Foxtemp2016viaJeelink}{defptr}{$addr} = $hash;

  AssignIoPort($hash);
  if (defined($hash->{IODev}->{NAME})) {
    Log3 $name, 3, "$name: I/O device is " . $hash->{IODev}->{NAME};
  } else {
    Log3 $name, 1, "$name: no I/O device";
  }

  return undef;
}


#-----------------------------------#
sub Foxtemp2016viaJeelink_Undef($$) {
  my ($hash, $arg) = @_;
  my $name = $hash->{NAME};
  my $addr = $hash->{addr};

  delete( $modules{Foxtemp2016viaJeelink}{defptr}{$addr} );

  return undef;
}


#-----------------------------------#
sub Foxtemp2016viaJeelink_Get($@) {
  my ($hash, $name, $cmd, @args) = @_;

  return "\"get $name\" needs at least one parameter" if(@_ < 3);

  my $list = "";

  return "Unknown argument $cmd, choose one of $list";
}

#-----------------------------------#
sub Foxtemp2016viaJeelink_Attr(@) {
  my ($cmd, $name, $attrName, $attrVal) = @_;

  return undef;
}


#-----------------------------------#
sub Foxtemp2016viaJeelink_Fingerprint($$) {
  my ($name, $msg) = @_;

  return ( "", $msg );
}


#-----------------------------------#
sub Foxtemp2016viaJeelink_Set($@) {
  my ($hash, $name, $cmd, $arg, $arg2) = @_;

  my $list = "";

  if( $cmd eq "nothingToSetHereYet" ) {
    #
  } else {
    return "Unknown argument $cmd, choose one of ".$list;
  }

  return undef;
}

#-----------------------------------#
sub Foxtemp2016viaJeelink_Parse($$) {
  my ($hash, $msg) = @_;
  my $name = $hash->{NAME};

  my ( @bytes, $addr, $temperature, $humidity );
  my $tempraw = 0xFFFF;
  my $humraw = 0xFFFF;
  my $batvolt = -1.0;

  if ($msg =~ m/^OK CC /) {
    # OK CC 8 247 98 194 159 169 198
    # c+p from main.c. Warning: All Perl offsets are off by 2!
    # Byte  2: Sensor-ID (0 - 255/0xff)
    # Byte  3: Sensortype (=0xf7 for FoxTemp)
    # Byte  4: temperature MSB (raw value from SHT31)
    # Byte  5: temperature LSB
    # Byte  6: humidity MSB (raw value from SHT31)
    # Byte  7: humidity LSB
    # Byte  8: Battery voltage
    @bytes = split( ' ', substr($msg, 6) );

    if (int(@bytes) != 7) {
      DoTrigger($name, "UNKNOWNCODE $msg");
      return "";
    }
    if ($bytes[1] != 0xF7) {
      DoTrigger($name, "UNKNOWNCODE $msg");
      return "";
    }

    #Log3 $name, 3, "$name: $msg cnt ".int(@bytes)." addr ".$bytes[0];

    $addr = sprintf( "%02x", $bytes[0] );
    $tempraw = ($bytes[2] << 8) | $bytes[3];
    $temperature = sprintf("%.2f", (-45.00 + 175.0 *($tempraw / 65535.0)));
    $humraw = ($bytes[4] << 8) | $bytes[5];
    $humidity = sprintf("%.2f", (100.0 * ($humraw / 65535.0)));
    $batvolt = sprintf("%.2f", (3.3 * $bytes[6] / 255.0_));
  } else {
    DoTrigger($name, "UNKNOWNCODE $msg");
    return "";
  }

  my $raddr = $addr;
  my $rhash = $modules{Foxtemp2016viaJeelink}{defptr}{$raddr};
  my $rname = $rhash ? $rhash->{NAME} : $raddr;

  return "" if( IsIgnored($rname) );

  if ( !$modules{Foxtemp2016viaJeelink}{defptr}{$raddr} ) {
    # get info about autocreate
    my $autoCreateState = 0;
    foreach my $d (keys %defs) {
      next if ($defs{$d}{TYPE} ne "autocreate");
      $autoCreateState = 1;
      $autoCreateState = 2 if(!AttrVal($defs{$d}{NAME}, "disable", undef));
    }

    # decide how to log
    my $loglevel = 4;
    if ($autoCreateState < 2) {
      $loglevel = 3;
    }

    Log3 $name, $loglevel, "Foxtemp2016viaJeelink: Unknown device $rname, please define it";

    return "";
  }

  my @list;
  push(@list, $rname);

  $rhash->{"Foxtemp2016viaJeelink_lastRcv"} = TimeNow();
  $rhash->{"sensorType"} = "Foxtemp2016viaJeelink";

  readingsBeginUpdate($rhash);

  # What is it good for? I haven't got the slightest clue, and the FHEM docu
  # about it could just as well be in Russian, it's absolutely not understandable
  # (at least for non-seasoned FHEM developers) what this is actually used for.
  readingsBulkUpdate($rhash, "state", "Initialized");
  # Round and write temperature and humidity
  if ($tempraw != 0xFFFF) { # 0xFFFF means the reading is invalid.
    readingsBulkUpdate($rhash, "temperature", $temperature);
    # Note: for humraw, 0xFFFF is a valid reading, so do not filter on that.
    readingsBulkUpdate($rhash, "humidity", $humidity);
  }

  if ($batvolt > 0.0) {
    readingsBulkUpdate($rhash, "batteryLevel", $batvolt);
  }

  readingsEndUpdate($rhash,1);

  return @list;
}


1;

=pod
=begin html

<a name="Foxtemp2016viaJeelink"></a>
<h3>Foxtemp2016viaJeelink</h3>
<ul>

  <tr><td>
  FHEM module for the Foxtemp20016-device.<br><br>

  It can be integrated into FHEM via a <a href="#JeeLink">JeeLink</a> as the IODevice.<br><br>

  On the JeeLink, you'll need to run a slightly modified version of the firmware
  for reading LaCrosse (found in the FHEM repository as
  /contrib/36_LaCrosse-LaCrosseITPlusReader.zip):
  It has support for a sensor type called "CustomSensor", but that is usually
  not compiled in. There is a line<br>
   <code>CustomSensor::AnalyzeFrame(payload);</code><br>
  commented out in <code>LaCrosseITPlusReader10.ino</code> -
  you need to remove the `////` to enable it, then recompile the firmware
  and flash the JeeLink.<br><br>

  You need to put this into /opt/fhem/FHEM/ and to make it work, you'll
  also need to modify 36_JeeLink.pm: to the string "clientsJeeLink" append
  ":Foxtemp2016viaJeelink".<br><br>

  <a name="Foxtemp2016viaJeelink_Define"></a>
  <b>Define</b>
  <ul>
    <code>define &lt;name&gt; Foxtemp2016viaJeelink &lt;addr&gt;</code> <br>
    <br>
    addr is the ID of the sensor, either in decimal or (prefixed with 0x) in hexadecimal notation.<br>
  </ul>
  <br>

  <a name="Foxtemp2016viaJeelink_Set"></a>
  <b>Set</b>
  <ul>
    <li>there is nothing to set here.</li>
  </ul><br>

  <a name="Foxtemp2016viaJeelink_Get"></a>
  <b>Get</b>
  <ul>
  </ul><br>

  <a name="Foxtemp2016viaJeelink_Readings"></a>
  <b>Readings</b>
  <ul>
    <li>batvolt (V)<br>
      the battery voltage in volts. The sensor should keep working until about
      1.0 volts.</li>
    <li>temperature (degC)<br>
      the temperature in degrees centigrade. Sorry, we don't support retard units.</li>
    <li>humidity (%rH)<br>
      the relative humidity in percent.</li>
  </ul><br>

  <a name="Foxtemp2016viaJeelink_Attr"></a>
  <b>Attributes</b>
  <ul>
    <li>ignore<br>
    1 -> ignore this device.</li>
  </ul><br>

  <b>Logging and autocreate</b><br>
  <ul>
  <li>If autocreate is not active (not defined or disabled) and LaCrosse is not contained in the ignoreTypes attribute of autocreate then
  the <i>Unknown device xx, please define it</i> messages will be logged with loglevel 3. In all other cases they will be logged with loglevel 4. </li>
  <li>The autocreateThreshold attribute of the autocreate module (see <a href="#autocreate">autocreate</a>) is respected. The default is 2:120, means, that
  autocreate will create a device for a sensor only, if the sensor was received at least two times within two minutes.</li>
  </ul>

</ul>

=end html
=cut
