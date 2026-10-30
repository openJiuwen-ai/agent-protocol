# Java SDK examples

This is a separate Maven artifact. `Rsa3072` and `PaymentExample` live in
`io.openjiuwen.a4p.examples` and are not included in the A4P Java SDK JAR.

From the repository root:

```sh
mvn -f A4P-java-sdk/pom.xml install
mvn -f A4P-java-sdk/examples/pom.xml test
mvn -f A4P-java-sdk/examples/pom.xml compile org.codehaus.mojo:exec-maven-plugin:3.6.3:java -Dexec.mainClass=io.openjiuwen.a4p.examples.PaymentExample
```

The RSA implementation is an example of the public `Signer` and
`SignatureVerifier` interfaces. Applications should supply implementations
appropriate for their own key management.
