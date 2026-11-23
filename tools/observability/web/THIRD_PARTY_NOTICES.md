# Third-party notices — dashboard web UI

The dashboard's v2 look (dark navy and gold palette, framed panels, display headings, stat tiles)
adapts ideas from these MIT-licensed projects. The stylesheets, fonts and markup here are original;
no source file is copied from them.

## Runtime-loaded 3D viewer

The optional UI v2 character preview loads Wowhead's model viewer script at
runtime (`wow.zamimg.com/modelviewer/classicplus/deployment/viewer/...`); it is
not vendored here and is served to the browser through the dashboard's own
cache so the page is not a third-party asset client. Its licence text names the
bundled MIT libraries (twgl.js, box3d, Emscripten runtime). Model metadata and
textures remain Wowhead/Zamimg content and are fetched only when the operator
turns the preview on.

## jQuery 3.7.1

`web/static/vendor/jquery-3.7.1.min.js` is the unmodified minified build from
<https://code.jquery.com/jquery-3.7.1.min.js>, MIT licensed:

Copyright OpenJS Foundation and other contributors, <https://openjsf.org/>

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## ForeverCMS

Copyright (c) 2026 FilinoDZ

## SahtoutCMS

Copyright (c) 2025 blodyiheb

Both projects are licensed under the MIT License:

Copyright (c) 2026 FilinoDZ
Copyright (c) 2025 blodyiheb

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
