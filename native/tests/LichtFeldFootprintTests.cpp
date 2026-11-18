#include <QFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <QVector3D>
#include <QtTest>
#include <array>
#include <cmath>

class LichtFeldFootprintTests final : public QObject {
  Q_OBJECT

private slots:
  void asymmetricPinholeAndOrthographicFootprints_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<QVector3D>("expected");
    QTest::addColumn<float>("tolerance");
    // Camera frame: 200 by 120; fx=100, fy=80; cx=140, cy=30 in GL pixels.
    // The asymmetric right and lower limits are +0.9 and −0.6. For
    // z=-10 and diag(4,9,1), Jx=(10,0,9), Jy=(0,8,-4.8).
    QTest::newRow("off-axis, clamped ray") << 0 << QVector3D(481.F, -43.2F, 599.04F) << .03F;
    // Orthographic derivatives ignore eye depth and principal point.
    QTest::newRow("orthographic affine") << 1 << QVector3D(40000.F, 0.F, 57600.F) << .03F;
    // At alpha=1, radius/(sqrt(2*log(510))) bounds only the major axis.
    QTest::newRow("opacity-aware radius") << 2 << QVector3D(400.F, 0.F, 21065.12F) << 2.F;
    QTest::newRow("small footprint unchanged") << 3 << QVector3D(25.F, 3.F, 16.F) << .002F;
  }

  void asymmetricPinholeAndOrthographicFootprints() {
    QFETCH(int, mode);
    QFETCH(QVector3D, expected);
    QFETCH(float, tolerance);
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    QVERIFY2(context.create(), "The OpenGL 3.3 contract must run, not be skipped");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    QVERIFY(surface.isValid());
    QVERIFY(context.makeCurrent(&surface));
    auto *gl = context.extraFunctions();
    gl->initializeOpenGLFunctions();

    QFile snippet(QStringLiteral(":/shaders/lichtfeld_preview_footprint.glsl"));
    QVERIFY(snippet.open(QIODevice::ReadOnly));
    QOpenGLShaderProgram program;
    QVERIFY2(program.addShaderFromSourceCode(QOpenGLShader::Vertex, R"GLSL(#version 330 core
void main() {
  vec2 corner = gl_VertexID == 0 ? vec2(-1, -1) : gl_VertexID == 1 ? vec2(3, -1) : vec2(-1, 3);
  gl_Position = vec4(corner, 0, 1);
}
)GLSL"), qPrintable(program.log()));
    QVERIFY2(program.addShaderFromSourceCode(QOpenGLShader::Fragment,
        QByteArray("#version 330 core\n") + snippet.readAll() + R"GLSL(
uniform int mode;
out vec4 result;
void main() {
  // Column vectors of the projection matrix; expected cx/cy were derived
  // directly from the independent 200×120 camera fixture.
  mat4 projection = mat4(vec4(1, 0, 0, 0), vec4(0, 1.333333333, 0, 0),
                         vec4(-0.4, 0.5, -1.002, -1), vec4(0, 0, -0.2, 0));
  if (mode == 1) {
    projection[2] = vec4(0, 0, -0.02, 0);
    projection[3] = vec4(0.4, -0.5, -1, 1);
  }
  vec3 cov = mode < 2 ? gswLichtFeldProjectCovariance(vec3(40, -24, -10),
      mat3(4, 0, 0, 0, 9, 0, 0, 0, 1), projection, vec2(200, 120),
      mode == 1) : gswLichtFeldClampFootprint(
      mode == 2 ? vec3(400, 0, 40000) : vec3(25, 3, 16), 1.0);
  result = vec4(cov, 1);
}
)GLSL"), qPrintable(program.log()));
    QVERIFY2(program.link() && program.bind(), qPrintable(program.log()));
    program.setUniformValue("mode", mode);
    QOpenGLFramebufferObjectFormat bufferFormat;
    bufferFormat.setInternalTextureFormat(GL_RGBA32F);
    QOpenGLFramebufferObject buffer(1, 1, bufferFormat);
    QVERIFY(buffer.isValid() && buffer.bind());
    GLuint array = 0;
    gl->glGenVertexArrays(1, &array);
    gl->glBindVertexArray(array);
    gl->glViewport(0, 0, 1, 1);
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    std::array<float, 4> pixel{};
    gl->glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, pixel.data());
    gl->glBindVertexArray(0);
    gl->glDeleteVertexArrays(1, &array);
    QCOMPARE(gl->glGetError(), GLenum(GL_NO_ERROR));
    QCOMPARE(pixel[3], 1.F);
    const QVector3D got(pixel[0], pixel[1], pixel[2]);
    qInfo() << "LichtFeld actual GLSL covariance" << got << "expected" << expected;
    for (int i = 0; i < 3; ++i) QVERIFY(std::abs(got[i] - expected[i]) <= tolerance);
  }
};

QTEST_MAIN(LichtFeldFootprintTests)
#include "LichtFeldFootprintTests.moc"
