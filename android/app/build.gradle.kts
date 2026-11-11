import java.util.Properties

plugins {
    id("com.android.application")
}

val releaseSigningFile = rootProject.file("keystore.properties")
val releaseSigning = Properties().apply {
    if (releaseSigningFile.isFile) releaseSigningFile.inputStream().use { load(it) }
}

android {
    namespace = "com.imelio.openmirrorphone"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.imelio.openmirrorphone"
        minSdk = 29
        targetSdk = 36
        versionCode = 3
        versionName = "0.1.0"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    if (releaseSigningFile.isFile) {
        signingConfigs {
            create("release") {
                storeFile = file(releaseSigning.getProperty("storeFile"))
                storePassword = releaseSigning.getProperty("storePassword")
                keyAlias = releaseSigning.getProperty("keyAlias")
                keyPassword = releaseSigning.getProperty("keyPassword")
            }
        }
        buildTypes {
            getByName("release") {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }
}

dependencies {
    implementation("com.journeyapps:zxing-android-embedded:4.3.0")
    implementation("androidx.core:core:1.6.0")
    implementation("androidx.fragment:fragment:1.3.6")
}
