Release process
===============

A release will be generated automatically for any version number tag
(of the form `v*`).  Release notes will be extracted from the contents
of the relevant section in [`CHANGELOG.md`][changelog].

The reduced-feature Secure Boot binaries will be signed using the
[iPXE Secure Boot CA][ipxesbca] and may be booted on a system with
UEFI Secure Boot enabled using the [iPXE shim][ipxeshim].

Steps
-----

1.  Edit the top-level [`Makefile`][makefile] to select values for
    `VERSION_MAJOR`, `VERSION_MINOR`, and `VERSION_PATCH`, and to set
    `EXTRAVERSION` to an empty string.

2.  Edit [`CHANGELOG.md`][changelog] to create a section and link for
    the new release.

3.  Commit these changes with a message such as:
    ```
    [release] Release version 1.2.3
    ```

4.  Tag the commit with the correct version number, e.g.:
    ```
    git tag v1.2.3
    ```

5.  Push the tag (and only the tag), e.g.:
    ```
    git push origin v1.2.3
    ```
    This will automatically create a [draft release][releases]
    including signed versions of the Secure Boot binaries.

6.  If all checks on the tag succeeded, then push the master branch as
    normal:
    ```
    git push
    ```

7.  Publish the [draft release][releases].

8.  Edit the top-level [`Makefile`][makefile] to set `EXTRAVERSION`
    back to the value `+`.

9.  Commit this change with a message such as:
    ```
    [release] Update version number after release
    ```

10. Push the master branch as normal:
    ```
    git push
    ```

Caveats
-------

Note that pushing the tag will trigger parts of the [build
workflow][workflow] that are not usually run.  In particular, the UEFI
Secure Boot signing stage will take place on a dedicated GitHub
Actions [runner][runners] that has access to a [hardware signing
token][ipxesbca].  There is a reasonable chance that parts of the
workflow may fail (e.g. due to an expired code signing certificate).
To recover from a failure, delete the tag (and the draft release, if
it has been created).

The signing step can be tested in isolation by pushing to the `sbsign`
branch, without the need to run through the whole release process.

[changelog]: CHANGELOG.md
[ipxesbca]: https://github.com/ipxe/secure-boot-ca
[ipxeshim]: https://github.com/ipxe/shim/releases/latest
[makefile]: src/Makefile
[releases]: https://github.com/ipxe/ipxe/releases
[runners]: https://github.com/ipxe/ipxe/settings/actions/runners
[workflow]: .github/workflows/build.yml
