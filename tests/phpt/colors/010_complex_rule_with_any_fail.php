@kphp_should_fail
/Error using highload with danger\-zone \(dangerZone\(\) call ssr\(\)\)/
/  dangerZone\(\) with following colors\: \{danger\-zone\}/
/  highload\(\) with following colors\: \{highload\}/
/  ssr\(\) with following colors\: \{ssr\}/
/Calling function without color danger\-zone in a function with color danger\-zone \(dangerZone\(\) call noHighload\(\)\)/
/  dangerZone\(\) with following colors\: \{danger\-zone\}/
/  noHighload\(\) with following colors\: \{no\-highload\}/
<?php

class KphpConfiguration {
  const FUNCTION_PALETTE = [
    "danger-zone *"           => "Calling function without color danger-zone in a function with color danger-zone",
    "danger-zone danger-zone" => 1,
    "danger-zone highload"    => 1,
    "danger-zone ssr"         => 1,

    "danger-zone highload *"        => "Error using highload with danger-zone",
    "danger-zone highload highload" => 1,
  ];
}

/**
 * @kphp-color danger-zone
 */
function dangerZone() {
    dangerZoneToo(); // ok
    highload(); // ok
    ssr(); // ok
    noHighload(); // error
}

/**
 * @kphp-color danger-zone
 */
function dangerZoneToo() {
    echo 1;
}

/**
 * @kphp-color highload
 */
function highload() {
    highloadToo(); // ok
    ssr(); // error
}

/**
 * @kphp-color has-curl
 */
function hasCurl() {
    echo 1;
}


/**
 * @kphp-color highload
 */
function highloadToo() {
    echo 1;
}

/**
 * @kphp-color ssr
 */
function ssr() {
    echo 1;
}

/**
 * @kphp-color no-highload
 */
function noHighload() {
    echo 1;
}

dangerZone();
