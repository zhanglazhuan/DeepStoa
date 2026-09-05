import StatsRow from '../components/StatsRow'
import Footer from '../components/Footer'

export default function AboutPage() {
  return (
    <>
      <section className="px-6 py-32 text-center lg:px-12">
        <h1 className="fluid-heading-xl mb-8">Learning deserves better tools</h1>
        <p className="mx-auto max-w-xl text-base leading-relaxed text-neutral-600">
          We believe students learn best when technology fades into the background.
          DeepStoa was built to give K-12 students the power of digital learning —
          without the distraction and eye strain of traditional screens.
        </p>
      </section>

      <section className="mx-auto max-w-4xl px-6 pb-40 lg:px-12">
        <StatsRow />
      </section>

      <Footer />
    </>
  )
}
