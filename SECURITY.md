# Security Policy

## General Strategy

"edj" by itself has no network access, and no ability to invoke other programs.  Rules
limit what script files can be imported by your main script.  A script can read any data
file, but only update files that were named on the command line EXCEPT for various
"writeXXX()" functions, which can be inhibited via a "-s" flag.

Plugins can provide network access and shell access.  Choose your plugins carefully.


## Supported Versions

I'm interested in security issues for any version of "edj" and its plugins.

My intent is to provide rapid responses to security issues in the current official release,
and the current development code available here.  Older versions will not be abandoned, but
the solution to security issues with older versions might be to upgrade.


## Reporting a Vulnerability

Report vulnerabilities to [kirkenda@gmail.com](mailto:kirkenda@gmail.com).  Please include
a description of how the vulnerability was detected (manual code inspection, automated code
inspection, real-world exploitation, etc).  Indicate which plugins you're using, and any
other relevant information.  If you can report a specific point in the code where the
vulnerability is centered, that'd help.

I will investigate and report back to you within (hopefully) a few days.  I may have more
questions.

If the vulnerability is valid, the fix is likely to involve downloaded the newest development
version of the code. I **may** issue a new release, but updating the development version is
quicker.
