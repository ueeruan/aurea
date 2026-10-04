package com.aurea.aurea.state

import androidx.core.content.FileProvider

/** Dedicated provider avoids device-specific failures from declaring the library class directly. */
class ExportFileProvider : FileProvider()
