import HeroSection from '../components/HeroSection'
import ValuePropSection from '../components/ValuePropSection'
import PersonaCards from '../components/PersonaCards'
import TestimonialSection from '../components/TestimonialSection'
import CTABanner from '../components/CTABanner'
import NewsletterSignup from '../components/NewsletterSignup'
import Footer from '../components/Footer'

export default function HomePage() {
  return (
    <>
      <HeroSection />
      <ValuePropSection />
      <PersonaCards />
      <TestimonialSection />
      <CTABanner />
      <NewsletterSignup />
      <Footer />
    </>
  )
}
