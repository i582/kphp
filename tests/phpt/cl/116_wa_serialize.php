@ok
<?php
require_once 'Classes/autoload.php';
require_once 'polyfills.php';

$a = new Classes\A();
echo serialize(instance_to_array($a));
