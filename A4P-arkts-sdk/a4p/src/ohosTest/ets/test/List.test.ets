import abilityTest from './Ability.test';
import a4pTest from './A4p.test';
import a4pCodecTest from './A4pCodec.test';
import a4pHttpTest from './A4pHttp.test';
import utilsTest from './Utils.test';

export default function testsuite() {
  abilityTest();
  a4pTest();
  a4pCodecTest();
  a4pHttpTest();
  utilsTest();
}
