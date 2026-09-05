import FeatureRow from '../components/FeatureRow'
import SpecsTable from '../components/SpecsTable'
import Footer from '../components/Footer'

export default function ProductPage() {
  return (
    <>
      <section className="px-6 py-32 text-center lg:px-12">
        <h1 className="fluid-heading-xl mb-6">The device built for learning</h1>
        <p className="text-base text-neutral-500">
          3.97" E-ink &middot; 800&times;480 &middot; Dual-touch &middot; ESP32S3 &middot; Zephyr OS
        </p>
      </section>

      <section className="mx-auto max-w-7xl space-y-32 px-6 pb-32 lg:px-12">
        <FeatureRow
          title="E-ink display. Zero eye strain."
          body="No backlight, no blue light, no glare. Students read comfortably for hours — just like paper, but smarter. The 3.97&quot; display is the perfect size for small hands."
          imageLeft
        />
        <FeatureRow
          title="Built for focus. Nothing else."
          body="No games, no social media, no notifications. DeepStoa does three things — reading, flashcard learning, and time management — and does them well."
        />
        <FeatureRow
          title="Weeks of battery. Zero friction."
          body="E-ink sips power. Students go weeks between charges. USB-C charging means one cable for everything. No charger anxiety, no daily plug-in ritual."
          imageLeft
        />
      </section>

      <section className="px-6 pb-32 lg:px-12">
        <SpecsTable />
      </section>

      <Footer />
    </>
  )
}
