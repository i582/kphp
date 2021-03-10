@kphp_should_fail
/Declaration of B::f\(\) must be compatible with A::f\(\)/
<?php

// Works in PHP, but fails in KPHP

class A {
  /** @param float $x */
  public function f($x) {}
}

class B extends A {
  public function f(int $x) {}
}

$b = new B();
